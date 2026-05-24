#include <lumex/lib/blocks.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/thread_roles.hpp>
#include <lumex/lib/tomlconfig.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/block_rebroadcaster.hpp>
#include <lumex/node/election.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/nodeconfig.hpp>

/*
 * block_rebroadcaster
 */

lumex::block_rebroadcaster::block_rebroadcaster (lumex::block_rebroadcaster_config const & config_a, lumex::node_flags const & flags_a, lumex::active_elections & active_a, lumex::network & network_a, lumex::stats & stats_a, lumex::logger & logger_a) :
	config{ config_a },
	flags{ flags_a },
	active{ active_a },
	network{ network_a },
	stats{ stats_a },
	logger{ logger_a },
	rebroadcasts{ config }
{
	if (!config.enable)
	{
		return;
	}

	// Rebroadcast blocks when they enter active elections
	active.election_started.add ([this] (std::shared_ptr<lumex::election> const & election, lumex::bucket_index, lumex::priority_timestamp) {
		if (auto block = election->winner ())
		{
			push (block);
		}
	});

	// Rebroadcast blocks when elections become stale
	active.election_stale.add ([this] (std::shared_ptr<lumex::election> const & election) {
		if (auto block = election->winner ())
		{
			push (block);
		}
	});
}

lumex::block_rebroadcaster::~block_rebroadcaster ()
{
	debug_assert (!thread.joinable ());
}

void lumex::block_rebroadcaster::start ()
{
	debug_assert (!thread.joinable ());

	if (!config.enable)
	{
		return;
	}

	thread = std::thread ([this] () {
		lumex::thread_role::set (lumex::thread_role::name::block_rebroadcasting);
		run ();
	});
}

void lumex::block_rebroadcaster::stop ()
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

bool lumex::block_rebroadcaster::push (std::shared_ptr<lumex::block> const & block)
{
	if (!config.enable)
	{
		return false;
	}

	bool added = false;
	{
		std::lock_guard guard{ mutex };

		auto const hash = block->hash ();

		// Don't queue if already in queue or queue is full
		if (!queue_dedup.contains (hash) && queue.size () < config.max_queue)
		{
			queue.push_back (block);
			queue_dedup.insert (hash);
			added = true;
		}
	}
	if (added)
	{
		stats.inc (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::queued);
		condition.notify_one ();
	}
	return added;
}

std::shared_ptr<lumex::block> lumex::block_rebroadcaster::next ()
{
	debug_assert (!mutex.try_lock ());
	debug_assert (!queue.empty ());

	auto block = queue.front ();
	queue.pop_front ();

	auto erased = queue_dedup.erase (block->hash ());
	debug_assert (erased == 1);

	return block;
}

void lumex::block_rebroadcaster::run ()
{
	std::unique_lock lock{ mutex };
	while (!stopped)
	{
		condition.wait (lock, [&] {
			return stopped || !queue.empty ();
		});

		if (stopped)
		{
			return;
		}

		stats.inc (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::loop);

		// Periodic cleanup of history
		if (cleanup_interval.elapse (lumex::is_dev_run () ? 1s : 60s))
		{
			lock.unlock ();
			cleanup ();
			lock.lock ();
		}

		// Wait for spare capacity if our network traffic is too high
		if (!check_capacity ())
		{
			stats.inc (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::cooldown);

			if (!queue.empty () && capacity_warning_interval.elapse (5s))
			{
				logger.warn (lumex::log::type::block_rebroadcaster, "Network capacity for block rebroadcasting unavailable, {} blocks waiting in queue", queue.size ());
			}

			lock.unlock ();
			std::this_thread::sleep_for (100ms);
			lock.lock ();
			continue; // Wait for more capacity
		}

		if (!queue.empty ())
		{
			// Only log if component is under pressure
			if (queue.size () > lumex::queue_warning_threshold () && log_interval.elapse (15s))
			{
				logger.info (lumex::log::type::block_rebroadcaster, "{} blocks in rebroadcast queue", queue.size ());
			}

			auto block = next ();
			auto const now = std::chrono::steady_clock::now ();
			auto const hash = block->hash ();

			lock.unlock ();

			bool should_broadcast = rebroadcasts->check_and_record (hash, now);
			if (should_broadcast)
			{
				stats.inc (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::rebroadcast);

				auto sent = broadcast (block);
				stats.add (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::sent, sent);
			}
			else
			{
				stats.inc (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::already_rebroadcasted);
			}

			lock.lock ();
		}
	}
}

size_t lumex::block_rebroadcaster::broadcast (std::shared_ptr<lumex::block> const & block)
{
	if (flags.super_rebroadcaster)
	{
		stats.inc (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::broadcast_super);
		return network.flood_block_all (block, lumex::transport::traffic_type::block_rebroadcast);
	}
	else
	{
		stats.inc (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::broadcast);
		return network.flood_block (block, lumex::transport::traffic_type::block_rebroadcast);
	}
}

bool lumex::block_rebroadcaster::check_capacity () const
{
	if (flags.super_rebroadcaster)
	{
		return network.check_capacity_ratio (lumex::transport::traffic_type::block_rebroadcast, 0.5f);
	}
	else
	{
		return network.check_capacity_fanout (lumex::transport::traffic_type::block_rebroadcast);
	}
}

void lumex::block_rebroadcaster::cleanup ()
{
	stats.inc (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::cleanup);

	auto erased = rebroadcasts->cleanup ();
	stats.add (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::erased, erased);
}

lumex::container_info lumex::block_rebroadcaster::container_info () const
{
	std::lock_guard guard{ mutex };

	lumex::container_info info;
	info.put ("queue", queue.size ());
	info.put ("queue_dedup", queue_dedup.size ());
	info.put ("rebroadcasts", rebroadcasts->size ());
	return info;
}

/*
 * block_rebroadcaster_index
 */

lumex::block_rebroadcaster_index::block_rebroadcaster_index (lumex::block_rebroadcaster_config const & config_a) :
	config{ config_a }
{
}

bool lumex::block_rebroadcaster_index::check_and_record (lumex::block_hash const & hash, std::chrono::steady_clock::time_point now)
{
	if (auto it = history.get<tag_hash> ().find (hash); it != history.get<tag_hash> ().end ())
	{
		// Check if enough time has passed since last rebroadcast
		if ((now - it->timestamp) < config.rebroadcast_cooldown)
		{
			return false; // Still in cooldown
		}

		// Update existing entry timestamp
		history.get<tag_hash> ().modify (it, [&] (auto & entry) {
			entry.timestamp = now;
		});
	}
	else
	{
		// Add new entry
		history.push_back (entry{ hash, now });
	}

	// Keep history size within limits, erase oldest entries
	while (history.size () > config.max_history)
	{
		history.pop_front ();
	}

	return true; // Should rebroadcast
}

size_t lumex::block_rebroadcaster_index::cleanup (std::chrono::steady_clock::time_point now)
{
	auto const cutoff = now - config.rebroadcast_cooldown;

	// Remove entries older than the threshold
	return erase_if (history, [&] (auto const & entry) {
		return entry.timestamp < cutoff;
	});
}

bool lumex::block_rebroadcaster_index::contains (lumex::block_hash const & hash) const
{
	return history.get<tag_hash> ().contains (hash);
}

size_t lumex::block_rebroadcaster_index::size () const
{
	return history.size ();
}

/*
 * block_rebroadcaster_config
 */

lumex::error lumex::block_rebroadcaster_config::deserialize (lumex::tomlconfig & toml)
{
	toml.get ("enable", enable);
	toml.get ("max_queue", max_queue);
	toml.get ("max_history", max_history);
	toml.get_duration ("rebroadcast_cooldown", rebroadcast_cooldown);

	return toml.get_error ();
}

lumex::error lumex::block_rebroadcaster_config::serialize (lumex::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Enable or disable block rebroadcasting when blocks enter active elections.\ntype:bool");
	toml.put ("max_queue", max_queue, "Maximum number of blocks to keep in queue for rebroadcasting.\ntype:uint64");
	toml.put ("max_history", max_history, "Maximum number of recently broadcast hashes to track for deduplication.\ntype:uint64");
	toml.put ("rebroadcast_cooldown", rebroadcast_cooldown.count (), "Minimum time between rebroadcasts for the same block.\ntype:milliseconds");

	return toml.get_error ();
}
