#include <lumex/lib/logging.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/thread_roles.hpp>
#include <lumex/lib/timer.hpp>
#include <lumex/lib/vote.hpp>
#include <lumex/node/node_observers.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/online_reps.hpp>
#include <lumex/node/rep_tiers.hpp>
#include <lumex/node/repcrawler.hpp>
#include <lumex/node/vote_cache.hpp>
#include <lumex/node/vote_processor.hpp>
#include <lumex/node/vote_router.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/ledger.hpp>

#include <chrono>

using namespace std::chrono_literals;

/*
 * vote_processor
 */

lumex::vote_processor::vote_processor (vote_processor_config const & config_a, lumex::vote_router & vote_router, lumex::node_observers & observers_a, lumex::stats & stats_a, lumex::node_flags & flags_a, lumex::logger & logger_a, lumex::online_reps & online_reps_a, lumex::rep_crawler & rep_crawler_a, lumex::ledger & ledger_a, lumex::network_params & network_params_a, lumex::rep_tiers & rep_tiers_a) :
	config{ config_a },
	vote_router{ vote_router },
	observers{ observers_a },
	stats{ stats_a },
	logger{ logger_a },
	online_reps{ online_reps_a },
	rep_crawler{ rep_crawler_a },
	ledger{ ledger_a },
	network_params{ network_params_a },
	rep_tiers{ rep_tiers_a }
{
	queue.max_size_query = [this] (auto const & origin) {
		switch (origin.source)
		{
			case lumex::rep_tier::tier_3:
			case lumex::rep_tier::tier_2:
			case lumex::rep_tier::tier_1:
				return config.max_pr_queue;
			case lumex::rep_tier::none:
				return config.max_non_pr_queue;
		}
		debug_assert (false);
		return size_t{ 0 };
	};

	queue.priority_query = [this] (auto const & origin) {
		switch (origin.source)
		{
			case lumex::rep_tier::tier_3:
				return config.pr_priority * config.pr_priority * config.pr_priority;
			case lumex::rep_tier::tier_2:
				return config.pr_priority * config.pr_priority;
			case lumex::rep_tier::tier_1:
				return config.pr_priority;
			case lumex::rep_tier::none:
				return size_t{ 1 };
		}
		debug_assert (false);
		return size_t{ 0 };
	};
}

lumex::vote_processor::~vote_processor ()
{
	debug_assert (threads.empty ());
}

void lumex::vote_processor::start ()
{
	debug_assert (threads.empty ());

	if (!config.enable)
	{
		return;
	}

	for (int n = 0; n < config.threads; ++n)
	{
		threads.emplace_back ([this] () {
			lumex::thread_role::set (lumex::thread_role::name::vote_processing);
			run ();
		});
	}
}

void lumex::vote_processor::stop ()
{
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();

	for (auto & thread : threads)
	{
		thread.join ();
	}
	threads.clear ();
}

bool lumex::vote_processor::vote (std::shared_ptr<lumex::vote> const & vote, std::shared_ptr<lumex::transport::channel> const & channel, lumex::vote_source source)
{
	debug_assert (channel != nullptr);

	auto const tier = rep_tiers.tier (vote->account);

	bool added = false;
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		added = queue.push ({ vote, source }, { tier, channel });
	}
	if (added)
	{
		stats.inc (lumex::stat::type::vote_processor, lumex::stat::detail::process);
		stats.inc (lumex::stat::type::vote_processor_tier, to_stat_detail (tier));
		stats.inc (lumex::stat::type::vote_processor_source, to_stat_detail (source));

		condition.notify_one ();
	}
	else
	{
		stats.inc (lumex::stat::type::vote_processor, lumex::stat::detail::overfill);
		stats.inc (lumex::stat::type::vote_processor_overfill, to_stat_detail (tier));
	}
	return added;
}

void lumex::vote_processor::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (lumex::stat::type::vote_processor, lumex::stat::detail::loop);

		if (!queue.empty ())
		{
			// Only log if component is under pressure
			if (queue.size () > lumex::queue_warning_threshold () && log_interval.elapse (15s))
			{
				logger.info (lumex::log::type::vote_processor, "{} votes (tier 3: {}, tier 2: {}, tier 1: {}) in processing queue",
				queue.size (),
				queue.size ({ lumex::rep_tier::tier_3 }),
				queue.size ({ lumex::rep_tier::tier_2 }),
				queue.size ({ lumex::rep_tier::tier_1 }));
			}

			run_batch (lock);
			debug_assert (!lock.owns_lock ());
			lock.lock ();
		}
		else
		{
			condition.wait (lock, [&] { return stopped || !queue.empty (); });
		}
	}
}

void lumex::vote_processor::run_batch (lumex::unique_lock<lumex::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());
	debug_assert (!queue.empty ());

	lumex::timer<std::chrono::milliseconds> timer;
	timer.start ();

	auto batch = queue.next_batch (config.batch_size);

	lock.unlock ();

	for (auto const & [item, origin] : batch)
	{
		auto const & [vote, source] = item;
		vote_blocking (vote, origin.channel, source);
	}

	total_processed += batch.size ();

	if (batch.size () == config.batch_size && timer.stop () > 100ms)
	{
		logger.debug (lumex::log::type::vote_processor, "Processed {} votes in {} milliseconds (rate of {} votes per second)",
		batch.size (),
		timer.value ().count (),
		((batch.size () * 1000ULL) / timer.value ().count ()));
	}
}

lumex::vote_code lumex::vote_processor::vote_blocking (std::shared_ptr<lumex::vote> const & vote, std::shared_ptr<lumex::transport::channel> const & channel, lumex::vote_source source)
{
	auto result = lumex::vote_code::invalid;
	if (!vote->validate ()) // false => valid vote
	{
		auto vote_results = vote_router.vote (vote, source);

		// Aggregate results for individual hashes
		bool replay = false;
		bool processed = false;
		bool late = false;

		for (auto const & [hash, hash_result] : vote_results)
		{
			replay |= (hash_result == lumex::vote_code::replay);
			processed |= (hash_result == lumex::vote_code::vote);
			late |= (hash_result == lumex::vote_code::late);
		}

		auto decide_result = [&] () {
			if (replay)
			{
				return lumex::vote_code::replay;
			}
			if (processed)
			{
				return lumex::vote_code::vote;
			}
			if (late)
			{
				return lumex::vote_code::late;
			}
			return lumex::vote_code::indeterminate;
		};

		result = decide_result ();

		observers.vote.notify (vote, channel, source, result);
	}

	stats.inc (lumex::stat::type::vote, to_stat_detail (result));

	logger.trace (lumex::log::type::vote_processor, lumex::log::detail::vote_processed,
	lumex::log::arg{ "vote", vote },
	lumex::log::arg{ "vote_source", source },
	lumex::log::arg{ "result", result });

	return result;
}

std::size_t lumex::vote_processor::size () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return queue.size ();
}

bool lumex::vote_processor::empty () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return queue.empty ();
}

lumex::container_info lumex::vote_processor::container_info () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	lumex::container_info info;
	info.put ("votes", queue.size ());
	info.add ("queue", queue.container_info ());
	return info;
}

/*
 * vote_cache_processor
 */

lumex::vote_cache_processor::vote_cache_processor (vote_processor_config const & config_a, lumex::vote_router & vote_router_a, lumex::vote_cache & vote_cache_a, lumex::stats & stats_a, lumex::logger & logger_a) :
	config{ config_a },
	vote_router{ vote_router_a },
	vote_cache{ vote_cache_a },
	stats{ stats_a },
	logger{ logger_a }
{
}

lumex::vote_cache_processor::~vote_cache_processor ()
{
	debug_assert (!thread.joinable ());
}

void lumex::vote_cache_processor::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread ([this] () {
		lumex::thread_role::set (lumex::thread_role::name::vote_cache_processing);
		run ();
	});
}

void lumex::vote_cache_processor::stop ()
{
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

void lumex::vote_cache_processor::trigger (lumex::block_hash const & hash)
{
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		if (triggered.size () >= config.max_triggered)
		{
			triggered.pop_front ();
			stats.inc (lumex::stat::type::vote_cache_processor, lumex::stat::detail::overfill);
		}
		triggered.push_back (hash);
	}
	condition.notify_all ();
	stats.inc (lumex::stat::type::vote_cache_processor, lumex::stat::detail::triggered);
}

void lumex::vote_cache_processor::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (lumex::stat::type::vote_cache_processor, lumex::stat::detail::loop);

		if (!triggered.empty ())
		{
			// Only log if component is under pressure
			if (triggered.size () > lumex::queue_warning_threshold () && log_interval.elapse (15s))
			{
				logger.info (lumex::log::type::vote_cache_processor, "{} hashes in processing queue", triggered.size ());
			}

			run_batch (lock);
			debug_assert (!lock.owns_lock ());
			lock.lock ();
		}
		else
		{
			condition.wait (lock, [&] { return stopped || !triggered.empty (); });
		}
	}
}

void lumex::vote_cache_processor::run_batch (lumex::unique_lock<lumex::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());
	debug_assert (!triggered.empty ());

	// Swap and deduplicate
	decltype (triggered) triggered_l;
	swap (triggered_l, triggered);

	lock.unlock ();

	std::unordered_set<lumex::block_hash> hashes;
	hashes.reserve (triggered_l.size ());
	hashes.insert (triggered_l.begin (), triggered_l.end ());

	stats.add (lumex::stat::type::vote_cache_processor, lumex::stat::detail::processed, hashes.size ());

	for (auto const & hash : hashes)
	{
		auto cached = vote_cache.find (hash);
		for (auto const & cached_vote : cached)
		{
			vote_router.vote (cached_vote, lumex::vote_source::cache, hash);
		}
	}
}

std::size_t lumex::vote_cache_processor::size () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return triggered.size ();
}

bool lumex::vote_cache_processor::empty () const
{
	return size () == 0;
}

lumex::container_info lumex::vote_cache_processor::container_info () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	lumex::container_info info;
	info.put ("triggered", triggered.size ());
	return info;
}

/*
 * vote_processor_config
 */

lumex::error lumex::vote_processor_config::serialize (lumex::tomlconfig & toml) const
{
	toml.put ("max_pr_queue", max_pr_queue, "Maximum number of votes to queue from principal representatives. \ntype:uint64");
	toml.put ("max_non_pr_queue", max_non_pr_queue, "Maximum number of votes to queue from non-principal representatives. \ntype:uint64");
	toml.put ("pr_priority", pr_priority, "Priority for votes from principal representatives. Higher priority gets processed more frequently. Non-principal representatives have a baseline priority of 1. \ntype:uint64");
	toml.put ("threads", threads, "Number of threads to use for processing votes. \ntype:uint64");
	toml.put ("batch_size", batch_size, "Maximum number of votes to process in a single batch. \ntype:uint64");

	return toml.get_error ();
}

lumex::error lumex::vote_processor_config::deserialize (lumex::tomlconfig & toml)
{
	toml.get ("max_pr_queue", max_pr_queue);
	toml.get ("max_non_pr_queue", max_non_pr_queue);
	toml.get ("pr_priority", pr_priority);
	toml.get ("threads", threads);
	toml.get ("batch_size", batch_size);

	return toml.get_error ();
}
