#include <lumex/lib/blocks.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/threading.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/node/block_processor.hpp>
#include <lumex/node/cementing_set.hpp>
#include <lumex/node/ledger_notifications.hpp>
#include <lumex/node/local_block_broadcaster.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/node.hpp>
#include <lumex/secure/ledger.hpp>

#include <boost/range/iterator_range.hpp>

lumex::local_block_broadcaster::local_block_broadcaster (local_block_broadcaster_config const & config_a, lumex::node & node_a, lumex::ledger_notifications & ledger_notifications_a, lumex::network & network_a, lumex::cementing_set & cementing_set_a, lumex::stats & stats_a, lumex::logger & logger_a) :
	config{ config_a },
	node{ node_a },
	ledger_notifications{ ledger_notifications_a },
	network{ network_a },
	cementing_set{ cementing_set_a },
	stats{ stats_a },
	logger{ logger_a },
	limiter{ config.broadcast_rate_limit, config.broadcast_rate_burst_ratio }
{
	if (!config.enable)
	{
		return;
	}

	ledger_notifications.blocks_processed.add ([this] (auto const & batch) {
		bool should_notify = false;
		for (auto const & [result, context] : batch)
		{
			// Only rebroadcast local blocks that were successfully processed (no forks or gaps)
			if (result == lumex::block_status::progress && context.source == lumex::block_source::local)
			{
				release_assert (context.block != nullptr);

				lumex::lock_guard<lumex::mutex> guard{ mutex };

				local_blocks.emplace_back (local_entry{ context.block, std::chrono::steady_clock::now () });
				stats.inc (lumex::stat::type::local_block_broadcaster, lumex::stat::detail::insert);

				// Erase oldest blocks if the queue gets too big
				while (local_blocks.size () > config.max_size)
				{
					stats.inc (lumex::stat::type::local_block_broadcaster, lumex::stat::detail::erase_oldest);
					local_blocks.pop_front ();
				}

				should_notify = true;
			}
		}
		if (should_notify)
		{
			condition.notify_all ();
		}
	});

	ledger_notifications.blocks_rolled_back.add ([this] (auto const & blocks, auto const & rollback_root) {
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		for (auto const & block : blocks)
		{
			auto erased = local_blocks.get<tag_hash> ().erase (block->hash ());
			stats.add (lumex::stat::type::local_block_broadcaster, lumex::stat::detail::rollback, erased);
		}
	});

	cementing_set.cemented_observers.add ([this] (auto const & block) {
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		auto erased = local_blocks.get<tag_hash> ().erase (block->hash ());
		stats.add (lumex::stat::type::local_block_broadcaster, lumex::stat::detail::cemented, erased);
	});
}

lumex::local_block_broadcaster::~local_block_broadcaster ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void lumex::local_block_broadcaster::start ()
{
	if (!config.enable)
	{
		return;
	}

	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		lumex::thread_role::set (lumex::thread_role::name::local_block_broadcasting);
		run ();
	} };
}

void lumex::local_block_broadcaster::stop ()
{
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	lumex::join_or_pass (thread);
}

bool lumex::local_block_broadcaster::contains (lumex::block_hash const & hash) const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	return local_blocks.get<tag_hash> ().contains (hash);
}

size_t lumex::local_block_broadcaster::size () const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	return local_blocks.size ();
}

void lumex::local_block_broadcaster::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait_for (lock, 1s);
		debug_assert ((std::this_thread::yield (), true)); // Introduce some random delay in debug builds

		if (!stopped && !local_blocks.empty ())
		{
			stats.inc (lumex::stat::type::local_block_broadcaster, lumex::stat::detail::loop);

			if (cleanup_interval.elapse (config.cleanup_interval))
			{
				cleanup (lock);
				debug_assert (lock.owns_lock ());
			}

			if (log_interval.elapse (15s))
			{
				logger.info (lumex::log::type::local_block_broadcaster, "{} blocks in local broadcasting set", local_blocks.size ());
			}

			run_broadcasts (lock);
			debug_assert (!lock.owns_lock ());
			lock.lock ();
		}
	}
}

std::chrono::milliseconds lumex::local_block_broadcaster::rebroadcast_interval (unsigned rebroadcasts) const
{
	return std::min (config.rebroadcast_interval * rebroadcasts, config.max_rebroadcast_interval);
}

void lumex::local_block_broadcaster::run_broadcasts (lumex::unique_lock<lumex::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());

	std::deque<local_entry> to_broadcast;

	auto const now = std::chrono::steady_clock::now ();

	// Iterate blocks with next_broadcast <= now
	auto & by_broadcast = local_blocks.get<tag_broadcast> ();
	for (auto const & entry : boost::make_iterator_range (by_broadcast.begin (), by_broadcast.upper_bound (now)))
	{
		debug_assert (entry.next_broadcast <= now);
		release_assert (entry.block != nullptr);
		to_broadcast.push_back (entry);
	}

	// Modify multi index container outside of the loop to avoid invalidating iterators
	auto & by_hash = local_blocks.get<tag_hash> ();
	for (auto const & entry : to_broadcast)
	{
		auto it = by_hash.find (entry.hash ());
		release_assert (it != by_hash.end ());
		bool success = by_hash.modify (it, [this, now] (auto & entry) {
			entry.rebroadcasts += 1;
			entry.last_broadcast = now;
			entry.next_broadcast = now + rebroadcast_interval (entry.rebroadcasts);
		});
		release_assert (success, "modify failed"); // Should never fail
	}

	lock.unlock ();

	for (auto const & entry : to_broadcast)
	{
		while (!limiter.should_pass (1))
		{
			std::this_thread::sleep_for (std::chrono::milliseconds{ 100 });
			if (stopped)
			{
				return;
			}
		}

		logger.debug (lumex::log::type::local_block_broadcaster, "Broadcasting block: {} (rebroadcasts so far: {})",
		entry.block->hash (),
		entry.rebroadcasts);

		stats.inc (lumex::stat::type::local_block_broadcaster, lumex::stat::detail::broadcast);

		auto sent = network.flood_block_initial (entry.block);

		stats.add (lumex::stat::type::local_block_broadcaster, lumex::stat::detail::sent, sent);
	}
}

void lumex::local_block_broadcaster::cleanup (lumex::unique_lock<lumex::mutex> & lock)
{
	debug_assert (!mutex.try_lock ());

	// Copy the local blocks to avoid holding the mutex during IO
	auto local_blocks_copy = local_blocks;

	lock.unlock ();

	std::set<lumex::block_hash> already_confirmed;
	{
		auto transaction = node.ledger.tx_begin_read ();
		for (auto const & entry : local_blocks_copy)
		{
			// This block has never been broadcasted, keep it so it's broadcasted at least once
			if (entry.last_broadcast == std::chrono::steady_clock::time_point{})
			{
				continue;
			}
			if (node.block_confirmed_or_being_confirmed (transaction, entry.block->hash ()))
			{
				stats.inc (lumex::stat::type::local_block_broadcaster, lumex::stat::detail::already_confirmed);
				already_confirmed.insert (entry.block->hash ());
			}
		}
	}

	lock.lock ();

	// Erase blocks that have been confirmed
	erase_if (local_blocks, [&already_confirmed] (auto const & entry) {
		return already_confirmed.contains (entry.block->hash ());
	});
}

lumex::container_info lumex::local_block_broadcaster::container_info () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	lumex::container_info info;
	info.put ("local", local_blocks);
	return info;
}

/*
 * local_block_broadcaster_config
 */

lumex::error lumex::local_block_broadcaster_config::serialize (lumex::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Enable or disable local block broadcasting.\ntype:bool");
	toml.put ("max_size", max_size, "Maximum number of blocks to keep in the local block broadcaster set. \ntype:uint64");
	toml.put ("rebroadcast_interval", rebroadcast_interval.count (), "Interval between rebroadcasts of the same block. Interval increases with each rebroadcast. \ntype:seconds");
	toml.put ("max_rebroadcast_interval", max_rebroadcast_interval.count (), "Maximum interval between rebroadcasts of the same block. \ntype:seconds");
	toml.put ("broadcast_rate_limit", broadcast_rate_limit, "Rate limit for broadcasting blocks. \ntype:uint64");
	toml.put ("broadcast_rate_burst_ratio", broadcast_rate_burst_ratio, "Burst ratio for broadcasting blocks. \ntype:double");
	toml.put ("cleanup_interval", cleanup_interval.count (), "Cleanup interval of the local blocks broadcaster set. \ntype:seconds");

	return toml.get_error ();
}

lumex::error lumex::local_block_broadcaster_config::deserialize (lumex::tomlconfig & toml)
{
	toml.get ("enable", enable);
	toml.get ("max_size", max_size);
	toml.get_duration ("rebroadcast_interval", rebroadcast_interval);
	toml.get_duration ("max_rebroadcast_interval", max_rebroadcast_interval);
	toml.get ("broadcast_rate_limit", broadcast_rate_limit);
	toml.get ("broadcast_rate_burst_ratio", broadcast_rate_burst_ratio);
	toml.get_duration ("cleanup_interval", cleanup_interval);

	return toml.get_error ();
}