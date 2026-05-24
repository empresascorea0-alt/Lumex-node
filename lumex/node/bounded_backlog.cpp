#include <lumex/lib/blocks.hpp>
#include <lumex/lib/config.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/thread_roles.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/backlog_scan.hpp>
#include <lumex/node/block_processor.hpp>
#include <lumex/node/bounded_backlog.hpp>
#include <lumex/node/cementing_set.hpp>
#include <lumex/node/ledger_notifications.hpp>
#include <lumex/node/local_block_broadcaster.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/scheduler/component.hpp>
#include <lumex/node/vote_cache.hpp>
#include <lumex/node/vote_router.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_set_any.hpp>
#include <lumex/secure/ledger_set_cemented.hpp>
#include <lumex/secure/transaction.hpp>

lumex::bounded_backlog::bounded_backlog (lumex::node_config const & config_a, lumex::node & node_a, lumex::ledger & ledger_a, lumex::ledger_notifications & ledger_notifications_a, lumex::bucketing & bucketing_a, lumex::backlog_scan & backlog_scan_a, lumex::block_processor & block_processor_a, lumex::cementing_set & cementing_set_a, lumex::stats & stats_a, lumex::logger & logger_a) :
	config{ config_a },
	node{ node_a },
	ledger{ ledger_a },
	ledger_notifications{ ledger_notifications_a },
	bucketing{ bucketing_a },
	backlog_scan{ backlog_scan_a },
	cementing_set{ cementing_set_a },
	stats{ stats_a },
	logger{ logger_a },
	scan_limiter{ config.bounded_backlog->scan_rate }
{
	if (!config.bounded_backlog->enable || ledger.max_backlog () == 0)
	{
		return;
	}

	// Activate accounts with unconfirmed blocks
	backlog_scan.batch_activated.add ([this] (auto const & batch) {
		auto transaction = ledger.tx_begin_read ();
		for (auto const & info : batch)
		{
			activate (transaction, info.account, info.account_info, info.conf_info);
		}
	});

	// Erase accounts with all confirmed blocks
	backlog_scan.batch_scanned.add ([this] (auto const & batch) {
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		for (auto const & info : batch)
		{
			if (info.conf_info.height == info.account_info.block_count)
			{
				index.erase (info.account);
			}
		}
	});

	// Track unconfirmed blocks
	ledger_notifications.blocks_processed.add ([this] (auto const & batch) {
		auto transaction = ledger.tx_begin_read ();
		for (auto const & [result, context] : batch)
		{
			if (result == lumex::block_status::progress)
			{
				auto const & block = context.block;
				insert (transaction, *block);
			}
		}
	});

	// Remove rolled back blocks from the backlog
	ledger_notifications.blocks_rolled_back.add ([this] (auto const & blocks, auto const & rollback_root) {
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		for (auto const & block : blocks)
		{
			index.erase (block->hash ());
		}
	});

	// Remove cemented blocks from the backlog
	cementing_set.batch_cemented.add ([this] (auto const & batch) {
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		for (auto const & context : batch)
		{
			index.erase (context.block->hash ());
		}
	});
}

lumex::bounded_backlog::~bounded_backlog ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
	debug_assert (!scan_thread.joinable ());
}

void lumex::bounded_backlog::start ()
{
	debug_assert (!thread.joinable ());

	if (!config.bounded_backlog->enable || ledger.max_backlog () == 0)
	{
		return;
	}

	boost::thread::attributes attrs;
	attrs.set_stack_size (lumex::ledger_thread_stack_size ());

	thread = boost::thread (attrs, [this] () {
		lumex::thread_role::set (lumex::thread_role::name::bounded_backlog);
		run ();
	});

	scan_thread = std::thread ([this] () {
		lumex::thread_role::set (lumex::thread_role::name::bounded_backlog_scan);
		run_scan ();
	});
}

void lumex::bounded_backlog::stop ()
{
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
	if (scan_thread.joinable ())
	{
		scan_thread.join ();
	}
}

size_t lumex::bounded_backlog::index_size () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return index.size ();
}

void lumex::bounded_backlog::activate (lumex::secure::transaction & transaction, lumex::account const & account, lumex::account_info const & account_info, lumex::confirmation_height_info const & conf_info)
{
	debug_assert (conf_info.frontier != account_info.head);

	// Insert blocks into the index starting from the account head block
	auto block = ledger.any.block_get (transaction, account_info.head);
	while (block)
	{
		// We reached the confirmed frontier, no need to track more blocks
		if (block->hash () == conf_info.frontier)
		{
			break;
		}
		// Check if the block is already in the backlog, avoids unnecessary ledger lookups
		if (contains (block->hash ()))
		{
			break;
		}

		bool inserted = insert (transaction, *block);

		// If the block was not inserted, we already have it in the backlog
		if (!inserted)
		{
			break;
		}

		transaction.refresh_if_needed ();

		block = ledger.any.block_get (transaction, block->previous ());
	}
}

void lumex::bounded_backlog::update (lumex::secure::transaction const & transaction, lumex::block_hash const & hash)
{
	// Erase if the block is either confirmed or missing
	if (!ledger.block_uncemented (transaction, hash))
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		index.erase (hash);
	}
}

bool lumex::bounded_backlog::insert (lumex::secure::transaction const & transaction, lumex::block const & block)
{
	auto const [priority_balance, priority_timestamp] = ledger.block_priority (transaction, block);
	auto const bucket_index = bucketing.bucket_index (priority_balance);

	lumex::lock_guard<lumex::mutex> guard{ mutex };

	return index.insert (block, bucket_index, priority_timestamp);
}

bool lumex::bounded_backlog::predicate () const
{
	debug_assert (!mutex.try_lock ());

	// Both ledger and tracked backlog must be over the threshold
	auto const max_backlog = ledger.max_backlog ();
	debug_assert (max_backlog > 0); // Should be fully disabled if max_backlog is 0

	return ledger.backlog_size () > max_backlog && index.size () > max_backlog;
}

void lumex::bounded_backlog::run ()
{
	std::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait_for (lock, 1s, [this] {
			return stopped || predicate ();
		});

		if (stopped)
		{
			return;
		}

		lock.unlock ();

		// Wait until all notification about the previous rollbacks are processed
		ledger_notifications.wait ([this] {
			stats.inc (lumex::stat::type::bounded_backlog, lumex::stat::detail::cooldown);
		});

		lock.lock ();

		stats.inc (lumex::stat::type::bounded_backlog, lumex::stat::detail::loop);

		// Calculate the number of targets to rollback
		auto const backlog = ledger.backlog_size ();
		auto const max_backlog = ledger.max_backlog ();
		uint64_t const target_count = backlog > max_backlog ? backlog - max_backlog : 0;

		if (target_count == 0)
		{
			continue;
		}

		auto const bucket_threshold = max_backlog / bucketing.size ();
		auto targets = gather_targets (std::min (target_count, static_cast<uint64_t> (config.bounded_backlog->batch_size)), bucket_threshold);
		if (!targets.empty ())
		{
			lock.unlock ();

			stats.add (lumex::stat::type::bounded_backlog, lumex::stat::detail::gathered_targets, targets.size ());
			auto processed = perform_rollbacks (targets, target_count);

			lock.lock ();

			// Erase rolled back blocks from the index
			for (auto const & hash : processed)
			{
				index.erase (hash);
			}
		}
		else
		{
			// Cooldown, this should not happen in normal operation
			stats.inc (lumex::stat::type::bounded_backlog, lumex::stat::detail::no_targets);
			condition.wait_for (lock, 100ms, [this] {
				return stopped.load ();
			});
		}
	}
}

bool lumex::bounded_backlog::should_rollback (lumex::block_hash const & hash) const
{
	if (node.vote_cache.contains (hash))
	{
		return false;
	}
	if (node.vote_router.contains (hash))
	{
		return false;
	}
	if (node.active.recently_confirmed.contains (hash))
	{
		return false;
	}
	if (node.scheduler.contains (hash))
	{
		return false;
	}
	if (node.cementing_set.contains (hash))
	{
		return false;
	}
	if (node.local_block_broadcaster.contains (hash))
	{
		return false;
	}
	return true;
}

std::deque<lumex::block_hash> lumex::bounded_backlog::perform_rollbacks (std::deque<lumex::block_hash> const & targets, size_t max_rollbacks)
{
	stats.inc (lumex::stat::type::bounded_backlog, lumex::stat::detail::performing_rollbacks);

	auto transaction = ledger.tx_begin_write (lumex::store::writer::bounded_backlog);

	std::deque<lumex::block_hash> processed;
	for (auto const & hash : targets)
	{
		// Skip the rollback if the block is being used by the node, this should be race free as it's checked while holding the ledger write lock
		if (!should_rollback (hash) || !ledger.block_uncemented (transaction, hash))
		{
			stats.inc (lumex::stat::type::bounded_backlog, lumex::stat::detail::rollback_skipped);
			continue;
		}

		// Here we check that the block is still OK to rollback, there could be a delay between gathering the targets and performing the rollbacks
		if (auto block = ledger.any.block_get (transaction, hash))
		{
			logger.debug (lumex::log::type::bounded_backlog, "Rolling back: {}, account: {}", hash, block->account ());

			std::deque<std::shared_ptr<lumex::block>> rollback_list;
			bool error = ledger.rollback (transaction, hash, rollback_list);
			if (error)
			{
				stats.inc (lumex::stat::type::bounded_backlog, lumex::stat::detail::rollback_failed);
				logger.warn (lumex::log::type::bounded_backlog, "Failed to roll back: {} (succeeded with {} dependents)", hash, rollback_list.size ());
			}
			else
			{
				stats.inc (lumex::stat::type::bounded_backlog, lumex::stat::detail::rollback);
				logger.debug (lumex::log::type::bounded_backlog, "Rolled back {} with {} dependents", hash, rollback_list.size ());
			}

			for (auto const & rollback : rollback_list)
			{
				processed.push_back (rollback->hash ());
			}

			if (!rollback_list.empty ())
			{
				// Notify observers of the rolled back blocks on a background thread, avoid dispatching notifications when holding ledger write transaction
				ledger_notifications.notify_rolled_back (transaction, std::move (rollback_list), block->qualified_root (), [this] {
					stats.inc (lumex::stat::type::bounded_backlog, lumex::stat::detail::notify_rolled_back);
				});
			}

			// Return early if we reached the maximum number of rollbacks
			if (processed.size () >= max_rollbacks)
			{
				break;
			}
		}
		else
		{
			stats.inc (lumex::stat::type::bounded_backlog, lumex::stat::detail::rollback_missing_block);
			processed.push_back (hash);
		}
	}

	// We had rocksdb issues in the past, ensure that rep weights are always consistent
	ledger.verify_consistency (transaction);

	return processed;
}

std::deque<lumex::block_hash> lumex::bounded_backlog::gather_targets (size_t max_count, size_t bucket_threshold) const
{
	debug_assert (!mutex.try_lock ());

	std::deque<lumex::block_hash> targets;

	// Start rolling back from lowest index buckets first
	for (auto bucket : bucketing.bucket_indices ())
	{
		// Only start rolling back if the bucket is over the threshold of unconfirmed blocks
		if (index.size (bucket) > bucket_threshold)
		{
			auto const count = std::min (max_count, config.bounded_backlog->batch_size);

			auto const top = index.top (bucket, count, [this] (auto const & hash) {
				// Only rollback if the block is not being used by the node
				return should_rollback (hash);
			});

			for (auto const & entry : top)
			{
				targets.push_back (entry);
			}
		}
	}

	return targets;
}

void lumex::bounded_backlog::run_scan ()
{
	std::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		auto wait = [&] (auto count) {
			while (!scan_limiter.should_pass (count))
			{
				condition.wait_for (lock, 100ms);
				if (stopped)
				{
					return;
				}
			}
		};

		lumex::block_hash last = 0;
		while (!stopped)
		{
			wait (config.bounded_backlog->batch_size);

			stats.inc (lumex::stat::type::bounded_backlog, lumex::stat::detail::loop_scan);

			auto batch = index.next (last, config.bounded_backlog->batch_size);
			if (batch.empty ()) // If batch is empty, we iterated over all accounts in the index
			{
				break;
			}

			lock.unlock ();
			{
				auto transaction = ledger.tx_begin_read ();
				for (auto const & hash : batch)
				{
					stats.inc (lumex::stat::type::bounded_backlog, lumex::stat::detail::scanned);
					update (transaction, hash);
					last = hash;
				}
			}
			lock.lock ();
		}
	}
}

bool lumex::bounded_backlog::contains (lumex::block_hash const & hash) const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return index.contains (hash);
}

lumex::container_info lumex::bounded_backlog::container_info () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	lumex::container_info info;
	info.put ("backlog", index.size ());
	info.add ("index", index.container_info ());
	return info;
}

/*
 * backlog_index
 */

bool lumex::backlog_index::insert (lumex::block const & block, lumex::bucket_index bucket, lumex::priority_timestamp priority)
{
	auto const hash = block.hash ();
	auto const account = block.account ();

	entry new_entry{
		.hash = hash,
		.account = account,
		.bucket = bucket,
		.priority = priority,
	};

	auto [it, inserted] = blocks.emplace (new_entry);
	if (inserted)
	{
		size_by_bucket[bucket]++;
		return true;
	}
	return false;
}

bool lumex::backlog_index::erase (lumex::account const & account)
{
	auto const [begin, end] = blocks.get<tag_account> ().equal_range (account);
	for (auto it = begin; it != end;)
	{
		size_by_bucket[it->bucket]--;
		it = blocks.get<tag_account> ().erase (it);
	}
	return begin != end;
}

bool lumex::backlog_index::erase (lumex::block_hash const & hash)
{
	if (auto existing = blocks.get<tag_hash> ().find (hash); existing != blocks.get<tag_hash> ().end ())
	{
		size_by_bucket[existing->bucket]--;
		blocks.get<tag_hash> ().erase (existing);
		return true;
	}
	return false;
}

std::deque<lumex::block_hash> lumex::backlog_index::top (lumex::bucket_index bucket, size_t count, filter_callback const & filter) const
{
	// Highest timestamp, lowest priority, iterate in descending order
	priority_key const starting_key{ bucket, std::numeric_limits<lumex::priority_timestamp>::max () };

	std::deque<lumex::block_hash> results;
	auto begin = blocks.get<tag_priority> ().lower_bound (starting_key);
	for (auto it = begin; it != blocks.get<tag_priority> ().end () && it->bucket == bucket && results.size () < count; ++it)
	{
		if (filter (it->hash))
		{
			results.push_back (it->hash);
		}
	}
	return results;
}

std::deque<lumex::block_hash> lumex::backlog_index::next (lumex::block_hash last, size_t count) const
{
	std::deque<block_hash> results;

	auto it = blocks.get<tag_hash_ordered> ().upper_bound (last);
	auto end = blocks.get<tag_hash_ordered> ().end ();

	for (; it != end && results.size () < count; ++it)
	{
		results.push_back (it->hash);
	}
	return results;
}

bool lumex::backlog_index::contains (lumex::block_hash const & hash) const
{
	return blocks.get<tag_hash> ().contains (hash);
}

size_t lumex::backlog_index::size () const
{
	return blocks.size ();
}

size_t lumex::backlog_index::size (lumex::bucket_index bucket) const
{
	if (auto it = size_by_bucket.find (bucket); it != size_by_bucket.end ())
	{
		return it->second;
	}
	return 0;
}

lumex::container_info lumex::backlog_index::container_info () const
{
	auto collect_bucket_sizes = [&] () {
		lumex::container_info info;
		for (auto [bucket, count] : size_by_bucket)
		{
			info.put (std::to_string (bucket), count);
		}
		return info;
	};

	lumex::container_info info;
	info.put ("blocks", blocks);
	info.add ("sizes", collect_bucket_sizes ());
	return info;
}

/*
 * bounded_backlog_config
 */

lumex::error lumex::bounded_backlog_config::serialize (lumex::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Enable the bounded backlog. \ntype:bool");
	toml.put ("batch_size", batch_size, "Maximum number of blocks to rollback per iteration. \ntype:uint64");
	toml.put ("scan_rate", scan_rate, "Rate limit for refreshing the backlog index. \ntype:uint64");

	return toml.get_error ();
}

lumex::error lumex::bounded_backlog_config::deserialize (lumex::tomlconfig & toml)
{
	toml.get ("enable", enable);
	toml.get ("batch_size", batch_size);
	toml.get ("scan_rate", scan_rate);

	return toml.get_error ();
}