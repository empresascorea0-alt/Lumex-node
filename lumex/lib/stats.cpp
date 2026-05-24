#include <lumex/lib/config.hpp>
#include <lumex/lib/env.hpp>
#include <lumex/lib/jsonconfig.hpp>
#include <lumex/lib/locks.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/stats_sinks.hpp>
#include <lumex/lib/thread_roles.hpp>
#include <lumex/lib/tomlconfig.hpp>

#include <boost/format.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <ctime>
#include <fstream>
#include <sstream>

#include <magic_enum.hpp>

using namespace std::chrono_literals;

// Static assertions to ensure our predefined array sizes are sufficient for the enums
// We check the _last value's integer value, which should be the highest valid index we need
static_assert (magic_enum::enum_integer (lumex::stat::type::_last) <= lumex::stats::types_count,
"stat::type enum has grown beyond the predefined array size. Increase types_count in stats.hpp");
static_assert (magic_enum::enum_integer (lumex::stat::detail::_last) <= lumex::stats::details_count,
"stat::detail enum has grown beyond the predefined array size. Increase details_count in stats.hpp");
static_assert (magic_enum::enum_integer (lumex::stat::dir::_last) <= lumex::stats::dirs_count,
"stat::dir enum has grown beyond the predefined array size. Increase dirs_count in stats.hpp");

/*
 * stat_log_sink
 */

std::string lumex::stat_log_sink::tm_to_string (tm & tm)
{
	return (boost::format ("%04d.%02d.%02d %02d:%02d:%02d") % (1900 + tm.tm_year) % (tm.tm_mon + 1) % tm.tm_mday % tm.tm_hour % tm.tm_min % tm.tm_sec).str ();
}

/*
 * stats
 */

lumex::stats::stats (lumex::logger & logger_a, lumex::stats_config config_a) :
	counters_impl{ std::make_unique<counters_array_t> () },
	counters{ *counters_impl },
	config{ std::move (config_a) },
	logger{ logger_a },
	enable_logging{ is_stat_logging_enabled () }
{
}

lumex::stats::~stats ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void lumex::stats::start ()
{
	if (!should_run ())
	{
		return;
	}

	thread = std::thread ([this] {
		lumex::thread_role::set (lumex::thread_role::name::stats);
		run ();
	});
}

void lumex::stats::stop ()
{
	{
		std::lock_guard guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

void lumex::stats::clear ()
{
	// Clear all counters
	for (auto & counter : counters)
	{
		counter.store (0, std::memory_order_relaxed);
	}

	// Clear samplers (still needs mutex)
	{
		std::lock_guard guard{ mutex };
		samplers.clear ();
		timestamp = std::chrono::steady_clock::now ();
	}
}

size_t lumex::stats::idx (stat::type type, stat::detail detail, stat::dir dir)
{
	// Dir is the slowest changing dimension, so it goes first for better cache locality
	auto type_idx = magic_enum::enum_integer (type);
	auto detail_idx = magic_enum::enum_integer (detail);
	auto dir_idx = magic_enum::enum_integer (dir);
	return (dir_idx * types_count * details_count) + (type_idx * details_count) + detail_idx;
}

std::atomic<lumex::stats::counter_value_t> & lumex::stats::counter_ref (stat::type type, stat::detail detail, stat::dir dir)
{
	auto index = idx (type, detail, dir);
	debug_assert (index < counters.size ());
	return counters[index];
}

std::atomic<lumex::stats::counter_value_t> const & lumex::stats::counter_ref (stat::type type, stat::detail detail, stat::dir dir) const
{
	auto index = idx (type, detail, dir);
	debug_assert (index < counters.size ());
	return counters[index];
}

void lumex::stats::add (stat::type type, stat::detail detail, stat::dir dir, counter_value_t value, bool aggregate_all)
{
	debug_assert (type != stat::type::_invalid);
	debug_assert (type != stat::type::_last);
	debug_assert (detail != stat::detail::_invalid);
	debug_assert (detail != stat::detail::_last);
	debug_assert (dir != stat::dir::_last);

	if (enable_logging)
	{
		logger.debug (lumex::log::type::stats, "Stat: {}::{}::{} += {}",
		to_string (type),
		to_string (detail),
		to_string (dir),
		value);
	}

	counter_ref (type, detail, dir).fetch_add (value, std::memory_order_relaxed);

	if (aggregate_all && detail != stat::detail::all)
	{
		counter_ref (type, stat::detail::all, dir).fetch_add (value, std::memory_order_relaxed);
	}
}

lumex::stats::counter_value_t lumex::stats::count (stat::type type, stat::detail detail, stat::dir dir) const
{
	return counter_ref (type, detail, dir).load (std::memory_order_relaxed);
}

lumex::stats::counter_value_t lumex::stats::count (stat::type type, stat::dir dir) const
{
	counter_value_t result = 0;

	// Sum all detail counters for this type and direction (except the 'all' detail)
	for (auto detail : magic_enum::enum_values<stat::detail> ())
	{
		if (detail != stat::detail::all && detail != stat::detail::_invalid && detail != stat::detail::_last)
		{
			result += counter_ref (type, detail, dir).load (std::memory_order_relaxed);
		}
	}
	return result;
}

void lumex::stats::sample (stat::sample sample, lumex::stats::sampler_value_t value, std::pair<sampler_value_t, sampler_value_t> expected_min_max)
{
	debug_assert (sample != stat::sample::_invalid);

	if (enable_logging)
	{
		logger.debug (lumex::log::type::stats, "Sample: {} -> {}", sample, value);
	}

	// Updates need to happen while holding the mutex
	auto update_sampler = [this, expected_min_max] (lumex::stats::sampler_key key, auto && updater) {
		// This is a two-step process to avoid exclusively locking the mutex in the common case
		{
			std::shared_lock lock{ mutex };

			if (auto it = samplers.find (key); it != samplers.end ())
			{
				updater (*it->second);

				return;
			}
		}
		// Not found, create a new entry
		{
			std::unique_lock lock{ mutex };

			// Insertions will be ignored if the key already exists
			auto [it, inserted] = samplers.emplace (key, std::make_unique<sampler_entry> (config.max_samples, expected_min_max));
			updater (*it->second);
		}
	};

	update_sampler (sampler_key{ sample }, [value] (sampler_entry & sampler) {
		sampler.add (value);
	});
}

auto lumex::stats::samples (stat::sample sample) -> std::vector<sampler_value_t>
{
	std::shared_lock lock{ mutex };
	if (auto it = samplers.find (sampler_key{ sample }); it != samplers.end ())
	{
		return it->second->collect ();
	}
	return {};
}

void lumex::stats::log_counters (stat_log_sink & sink)
{
	// TODO: Replace with a proper std::chrono time
	std::time_t time = std::chrono::system_clock::to_time_t (std::chrono::system_clock::now ());
	tm local_tm = *localtime (&time);

	std::lock_guard guard{ mutex };
	log_counters_impl (sink, local_tm);
}

void lumex::stats::log_counters_impl (stat_log_sink & sink, tm & tm)
{
	sink.begin ();

	if (sink.entries () >= config.log_rotation_count)
	{
		sink.rotate ();
	}

	if (config.log_headers)
	{
		auto walltime (std::chrono::system_clock::now ());
		sink.write_header ("counters", walltime);
	}

	for (auto dir : magic_enum::enum_values<stat::dir> ())
	{
		if (dir == stat::dir::_last)
			continue;

		for (auto type : magic_enum::enum_values<stat::type> ())
		{
			if (type == stat::type::_invalid || type == stat::type::_last)
				continue;

			for (auto detail : magic_enum::enum_values<stat::detail> ())
			{
				if (detail == stat::detail::_invalid || detail == stat::detail::_last)
					continue;

				auto value = counter_ref (type, detail, dir).load (std::memory_order_relaxed);

				if (value > 0) // Only log non-zero counters
				{
					std::string type_str{ to_string (type) };
					std::string detail_str{ to_string (detail) };
					std::string dir_str{ to_string (dir) };

					sink.write_counter_entry (tm, type_str, detail_str, dir_str, value);
				}
			}
		}
	}

	sink.entries ()++;
	sink.finalize ();
}

void lumex::stats::log_samples (stat_log_sink & sink)
{
	// TODO: Replace with a proper std::chrono time
	std::time_t time = std::chrono::system_clock::to_time_t (std::chrono::system_clock::now ());
	tm local_tm = *localtime (&time);

	std::lock_guard guard{ mutex };
	log_samples_impl (sink, local_tm);
}

void lumex::stats::log_samples_impl (stat_log_sink & sink, tm & tm)
{
	sink.begin ();
	if (sink.entries () >= config.log_rotation_count)
	{
		sink.rotate ();
	}

	if (config.log_headers)
	{
		auto walltime (std::chrono::system_clock::now ());
		sink.write_header ("samples", walltime);
	}

	for (auto const & [key, entry] : samplers)
	{
		std::string sample{ to_string (key.sample) };

		sink.write_sampler_entry (tm, sample, entry->collect (), entry->expected_min_max);
	}

	sink.entries ()++; // TODO: This `++` looks like a hack, needs a redesign
	sink.finalize ();
}

bool lumex::stats::should_run () const
{
	if (config.log_counters_interval.count () > 0)
	{
		return true;
	}
	if (config.log_samples_interval.count () > 0)
	{
		return true;
	}
	return false;
}

void lumex::stats::run ()
{
	std::unique_lock lock{ mutex };
	while (!stopped)
	{
		condition.wait_for (lock, 1s, [this] {
			return stopped;
		});
		if (!stopped)
		{
			run_one (lock);
			debug_assert (lock.owns_lock ());
		}
	}
}

void lumex::stats::run_one (std::unique_lock<std::shared_mutex> & lock)
{
	static stat_file_writer log_count{ config.log_counters_filename };
	static stat_file_writer log_sample{ config.log_samples_filename };

	debug_assert (!mutex.try_lock ());
	debug_assert (lock.owns_lock ());

	// TODO: Replace with a proper std::chrono time
	std::time_t time = std::chrono::system_clock::to_time_t (std::chrono::system_clock::now ());
	tm local_tm = *localtime (&time);

	// Counters
	if (config.log_counters_interval.count () > 0)
	{
		if (lumex::elapse (log_last_count_writeout, config.log_counters_interval))
		{
			log_counters_impl (log_count, local_tm);
		}
	}

	// Samples
	if (config.log_samples_interval.count () > 0)
	{
		if (lumex::elapse (log_last_sample_writeout, config.log_samples_interval))
		{
			log_samples_impl (log_sample, local_tm);
		}
	}
}

std::chrono::seconds lumex::stats::last_reset ()
{
	std::lock_guard guard{ mutex };
	auto now (std::chrono::steady_clock::now ());
	return std::chrono::duration_cast<std::chrono::seconds> (now - timestamp);
}

std::string lumex::stats::dump (category category)
{
	stat_json_writer sink;
	switch (category)
	{
		case category::counters:
			log_counters (sink);
			break;
		case category::samples:
			log_samples (sink);
			break;
		default:
			debug_assert (false, "missing stat_category case");
	}
	return sink.to_string ();
}

bool lumex::stats::is_stat_logging_enabled ()
{
	static auto const enabled = [] () {
		if (auto value = lumex::env::get<bool> ("LUMEX_LOG_STATS"))
		{
			std::cerr << "Stats logging enabled by LUMEX_LOG_STATS environment variable" << std::endl;
			return *value;
		}
		return false;
	}();
	return enabled;
}

/*
 * stats::sampler_entry
 */

void lumex::stats::sampler_entry::add (lumex::stats::sampler_value_t value)
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	samples.push_back (value);
}

auto lumex::stats::sampler_entry::collect () -> std::vector<sampler_value_t>
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	std::vector<sampler_value_t> result{ samples.begin (), samples.end () };
	samples.clear ();
	return result;
}

/*
 * stats_config
 */

lumex::error lumex::stats_config::serialize_toml (lumex::tomlconfig & toml) const
{
	toml.put ("max_samples", max_samples, "Maximum number of samples to keep in the ring buffer.\ntype:uint64");

	lumex::tomlconfig log_l;
	log_l.put ("headers", log_headers, "If true, write headers on each counter or samples writeout.\nThe header contains log type and the current wall time.\ntype:bool");
	log_l.put ("interval_counters", log_counters_interval.count (), "How often to log counters. 0 disables logging.\ntype:milliseconds");
	log_l.put ("interval_samples", log_samples_interval.count (), "How often to log samples. 0 disables logging.\ntype:milliseconds");
	log_l.put ("rotation_count", log_rotation_count, "Maximum number of log outputs before rotating the file.\ntype:uint64");
	log_l.put ("filename_counters", log_counters_filename, "Log file name for counters.\ntype:string");
	log_l.put ("filename_samples", log_samples_filename, "Log file name for samples.\ntype:string");
	toml.put_child ("log", log_l);

	return toml.get_error ();
}

lumex::error lumex::stats_config::deserialize_toml (lumex::tomlconfig & toml)
{
	toml.get ("max_samples", max_samples);

	if (auto maybe_log_l = toml.get_optional_child ("log"))
	{
		auto log_l = *maybe_log_l;

		log_l.get ("headers", log_headers);

		auto counters_interval_l = log_counters_interval.count ();
		log_l.get ("interval_counters", counters_interval_l);
		log_counters_interval = std::chrono::milliseconds{ counters_interval_l };

		auto samples_interval_l = log_samples_interval.count ();
		log_l.get ("interval_samples", samples_interval_l);
		log_samples_interval = std::chrono::milliseconds{ samples_interval_l };

		log_l.get ("rotation_count", log_rotation_count);
		log_l.get ("filename_counters", log_counters_filename);
		log_l.get ("filename_samples", log_samples_filename);

		// Don't allow specifying the same file name for counter and samples logs
		if (log_counters_filename == log_samples_filename)
		{
			toml.get_error ().set ("The statistics counter and samples config values must be different");
		}
	}

	return toml.get_error ();
}
