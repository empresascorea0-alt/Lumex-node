#include <lumex/lib/blocks.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/bucketing.hpp>
#include <lumex/node/cementing_set.hpp>
#include <lumex/node/election.hpp>
#include <lumex/node/ledger_notifications.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/scheduler/priority.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_set_any.hpp>
#include <lumex/secure/ledger_set_cemented.hpp>
#include <lumex/store/ledger/confirmation_height.hpp>

lumex::scheduler::priority::priority (lumex::node_config & node_config, lumex::node & node_a, lumex::ledger & ledger_a, lumex::ledger_notifications & ledger_notifications_a, lumex::bucketing & bucketing_a, lumex::active_elections & active_a, lumex::cementing_set & cementing_set_a, lumex::stats & stats_a, lumex::logger & logger_a) :
	config{ *node_config.priority_scheduler },
	node{ node_a },
	ledger{ ledger_a },
	ledger_notifications{ ledger_notifications_a },
	bucketing{ bucketing_a },
	active{ active_a },
	cementing_set{ cementing_set_a },
	stats{ stats_a },
	logger{ logger_a },
	pool{ config.max_blocks, config.reserved_blocks }
{
	for (auto const & index : bucketing.bucket_indices ())
	{
		buckets[index] = std::make_unique<scheduler::bucket> (index, config, active, stats, logger);
	}

	if (!config.enable)
	{
		return;
	}

	// Activate accounts with fresh blocks
	ledger_notifications.blocks_processed.add ([this] (auto const & batch) {
		auto transaction = ledger.tx_begin_read ();
		for (auto const & [result, context] : batch)
		{
			if (result == lumex::block_status::progress)
			{
				release_assert (context.block != nullptr);
				activate (transaction, context.block->account ());
			}
		}
	});

	// Activate successors of cemented blocks
	cementing_set.batch_cemented.add ([this] (auto const & batch) {
		if (node.flags.disable_activate_successors)
		{
			return;
		}

		auto transaction = ledger.tx_begin_read ();
		for (auto const & context : batch)
		{
			release_assert (context.block != nullptr);
			activate_successors (transaction, *context.block);
		}
	});
}

lumex::scheduler::priority::~priority ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
	debug_assert (!cleanup_thread.joinable ());
}

void lumex::scheduler::priority::notify ()
{
	condition.notify_all ();
}

void lumex::scheduler::priority::start ()
{
	debug_assert (!thread.joinable ());
	debug_assert (!cleanup_thread.joinable ());

	if (!config.enable)
	{
		return;
	}

	thread = std::thread{ [this] () {
		lumex::thread_role::set (lumex::thread_role::name::scheduler_priority);
		run ();
	} };

	cleanup_thread = std::thread{ [this] () {
		lumex::thread_role::set (lumex::thread_role::name::scheduler_priority);
		run_cleanup ();
	} };
}

void lumex::scheduler::priority::stop ()
{
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	join_or_pass (thread);
	join_or_pass (cleanup_thread);
}

bool lumex::scheduler::priority::activate (secure::transaction const & transaction, lumex::account const & account)
{
	debug_assert (!account.is_zero ());
	if (auto info = ledger.any.account_get (transaction, account))
	{
		lumex::confirmation_height_info conf_info;
		ledger.store.confirmation_height.get (transaction, account, conf_info);
		if (conf_info.height < info->block_count)
		{
			return activate (transaction, account, *info, conf_info);
		}
	}
	stats.inc (lumex::stat::type::election_scheduler, lumex::stat::detail::activate_skip);
	return false; // Not activated
}

bool lumex::scheduler::priority::activate (secure::transaction const & transaction, lumex::account const & account, lumex::account_info const & account_info, lumex::confirmation_height_info const & conf_info)
{
	debug_assert (conf_info.frontier != account_info.head);

	auto const hash = conf_info.height == 0 ? account_info.open_block : ledger.any.block_successor (transaction, conf_info.frontier).value_or (0);
	auto const block = ledger.any.block_get (transaction, hash);
	if (!block)
	{
		return false; // Not activated
	}

	if (ledger.dependencies_cemented (transaction, *block))
	{
		auto const [priority_balance, priority_timestamp] = ledger.block_priority (transaction, *block);
		auto const bucket_index = bucketing.bucket_index (priority_balance);

		bool added = false;
		{
			lumex::lock_guard<lumex::mutex> guard{ mutex };
			added = pool.push (block, bucket_index, priority_timestamp);
		}
		if (added)
		{
			stats.inc (lumex::stat::type::election_scheduler, lumex::stat::detail::activated);

			logger.debug (lumex::log::type::election_scheduler, "Activated block: {} for account: {} (bucket: {}, priority timestamp: {})",
			block->hash (), account, bucket_index, priority_timestamp);

			logger.trace (lumex::log::type::election_scheduler, lumex::log::detail::block_activated,
			lumex::log::arg{ "account", account },
			lumex::log::arg{ "block", block },
			lumex::log::arg{ "time", account_info.modified },
			lumex::log::arg{ "priority_balance", priority_balance },
			lumex::log::arg{ "priority_timestamp", priority_timestamp });

			condition.notify_all ();
		}
		else
		{
			stats.inc (lumex::stat::type::election_scheduler, lumex::stat::detail::activate_full);
		}

		return true; // Activated
	}

	stats.inc (lumex::stat::type::election_scheduler, lumex::stat::detail::activate_failed);
	return false; // Not activated
}

bool lumex::scheduler::priority::push (std::shared_ptr<lumex::block> const & block, lumex::bucket_index bucket, lumex::priority_timestamp priority)
{
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		if (!pool.push (block, bucket, priority))
		{
			return false;
		}
	}
	condition.notify_all ();
	return true;
}

bool lumex::scheduler::priority::activate_successors (secure::transaction const & transaction, lumex::block const & block)
{
	bool result = activate (transaction, block.account ());

	// Start or vote for the next unconfirmed block in the destination account
	if (block.is_send () && !block.destination ().is_zero () && block.destination () != block.account ())
	{
		result |= activate (transaction, block.destination ());
	}

	return result;
}

bool lumex::scheduler::priority::contains (lumex::block_hash const & hash) const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return pool.contains (hash);
}

std::size_t lumex::scheduler::priority::size () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return pool.size ();
}

bool lumex::scheduler::priority::empty () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return pool.empty ();
}

size_t lumex::scheduler::priority::pool_size () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return pool.size ();
}

size_t lumex::scheduler::priority::pool_size (lumex::bucket_index bucket) const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return pool.size (bucket);
}

size_t lumex::scheduler::priority::election_count (lumex::bucket_index bucket) const
{
	auto it = buckets.find (bucket);
	return it != buckets.end () ? it->second->election_count () : 0;
}

bool lumex::scheduler::priority::predicate () const
{
	debug_assert (!mutex.try_lock ());
	auto tops = pool.top_all ();
	return std::any_of (buckets.begin (), buckets.end (), [&tops] (auto const & bucket) {
		return bucket.second->available (find_or_default (tops, bucket.first));
	});
}

void lumex::scheduler::priority::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait (lock, [this] () {
			return stopped || predicate ();
		});

		debug_assert ((std::this_thread::yield (), true)); // Introduce some random delay in debug builds

		if (stopped)
		{
			return;
		}

		stats.inc (lumex::stat::type::election_scheduler, lumex::stat::detail::loop);

		// Get the top blocks for each bucket
		auto tops = pool.top_all ();

		lock.unlock ();

		std::deque<lumex::block_hash> activated;

		for (auto const & [index, top] : tops)
		{
			auto const & bucket = buckets.at (index);

			if (bucket->available (top))
			{
				bucket->activate (top);
				activated.push_back (top.block->hash ());
			}
		}

		batch_activated.notify (activated); // Notify without the lock held

		lock.lock ();

		// Erase activated blocks from the pool
		if (!activated.empty ())
		{
			pool.erase_all (activated);
		}
	}
}

void lumex::scheduler::priority::run_cleanup ()
{
	// As long as there is work done, keep the cleanup loop busy with shorter sleeps
	bool did_work = false;

	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait_for (lock, did_work ? config.cleanup_interval / 10 : config.cleanup_interval, [this] () {
			return stopped;
		});

		did_work = false;

		if (stopped)
		{
			return;
		}

		stats.inc (lumex::stat::type::election_scheduler, lumex::stat::detail::cleanup);

		lock.unlock ();

		for (auto const & [index, bucket] : buckets)
		{
			did_work |= bucket->cleanup ();
		}

		lock.lock ();
	}
}

lumex::container_info lumex::scheduler::priority::container_info () const
{
	auto collect_elections = [&] () {
		lumex::container_info info;
		for (auto const & [index, bucket] : buckets)
		{
			info.put (std::to_string (index), bucket->election_count ());
		}
		return info;
	};

	lumex::lock_guard<lumex::mutex> guard{ mutex };

	lumex::container_info info;
	info.add ("elections", collect_elections ());
	info.add ("pool", pool.container_info ());
	return info;
}

/*
 * priority_config
 */

lumex::error lumex::scheduler::priority_config::serialize (lumex::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Enable priority scheduler. \nType: bool");
	toml.put ("max_blocks", max_blocks, "Total shared pool size across all buckets. \nType: uint64");
	toml.put ("reserved_blocks", reserved_blocks, "Reserved blocks per bucket. \nType: uint64");
	toml.put ("reserved_elections", reserved_elections, "Guaranteed election slots per bucket. \nType: uint64");
	toml.put ("max_elections", max_elections, "Maximum election slots per bucket when AEC has space. \nType: uint64");
	toml.put ("cleanup_interval", cleanup_interval.count (), "Interval between cleanup runs when idle. \nType: milliseconds");

	return toml.get_error ();
}

lumex::error lumex::scheduler::priority_config::deserialize (lumex::tomlconfig & toml)
{
	toml.get ("enable", enable);
	toml.get ("max_blocks", max_blocks);
	toml.get ("reserved_blocks", reserved_blocks);
	toml.get ("reserved_elections", reserved_elections);
	toml.get ("max_elections", max_elections);
	toml.get_duration ("cleanup_interval", cleanup_interval);

	return toml.get_error ();
}