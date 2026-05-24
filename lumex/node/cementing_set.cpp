#include <lumex/lib/blocks.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/thread_roles.hpp>
#include <lumex/node/block_processor.hpp>
#include <lumex/node/cementing_set.hpp>
#include <lumex/node/ledger_notifications.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_set_any.hpp>
#include <lumex/secure/ledger_set_cemented.hpp>
#include <lumex/store/write_queue.hpp>

lumex::cementing_set::cementing_set (cementing_set_config const & config_a, lumex::ledger & ledger_a, lumex::ledger_notifications & ledger_notifications_a, lumex::stats & stats_a, lumex::logger & logger_a) :
	config{ config_a },
	ledger{ ledger_a },
	ledger_notifications{ ledger_notifications_a },
	stats{ stats_a },
	logger{ logger_a },
	workers{ 1, lumex::thread_role::name::confirmation_height_notifications }
{
	batch_cemented.add ([this] (auto const & cemented) {
		for (auto const & context : cemented)
		{
			cemented_observers.notify (context.block);
		}
	});

	// Requeue blocks that failed to cement immediately due to missing ledger blocks
	ledger_notifications.blocks_processed.add ([this] (auto const & batch) {
		bool should_notify = false;
		{
			std::lock_guard lock{ mutex };
			for (auto const & [result, context] : batch)
			{
				if (auto it = deferred.get<tag_hash> ().find (context.block->hash ()); it != deferred.get<tag_hash> ().end ())
				{
					stats.inc (lumex::stat::type::cementing_set, lumex::stat::detail::requeued);
					set.push_back (*it);
					deferred.get<tag_hash> ().erase (it);
					should_notify = true;
				}
			}
		}
		if (should_notify)
		{
			condition.notify_all ();
		}
	});
}

lumex::cementing_set::~cementing_set ()
{
	debug_assert (!thread.joinable ());
}

bool lumex::cementing_set::add (lumex::block_hash const & hash, std::shared_ptr<lumex::election> const & election)
{
	bool added = false;
	{
		std::lock_guard lock{ mutex };
		auto [it, inserted] = set.push_back ({ hash, election });
		added = inserted;
	}
	if (added)
	{
		condition.notify_all ();
		stats.inc (lumex::stat::type::cementing_set, lumex::stat::detail::insert);
	}
	else
	{
		stats.inc (lumex::stat::type::cementing_set, lumex::stat::detail::duplicate);
	}
	return added;
}

void lumex::cementing_set::start ()
{
	debug_assert (!thread.joinable ());

	if (!config.enable)
	{
		return;
	}

	workers.start ();

	thread = std::thread{ [this] () {
		lumex::thread_role::set (lumex::thread_role::name::confirmation_height);
		run ();
	} };
}

void lumex::cementing_set::stop ()
{
	{
		std::lock_guard lock{ mutex };
		stopped = true;
		condition.notify_all ();
	}
	if (thread.joinable ())
	{
		thread.join ();
	}
	workers.stop ();
}

bool lumex::cementing_set::contains (lumex::block_hash const & hash) const
{
	std::lock_guard lock{ mutex };
	return set.get<tag_hash> ().contains (hash) || deferred.get<tag_hash> ().contains (hash) || current.contains (hash);
}

std::size_t lumex::cementing_set::size () const
{
	// Do not report deferred blocks, as they are not currently being processed (and might never be requeued)
	std::lock_guard lock{ mutex };
	return set.size () + current.size ();
}

std::size_t lumex::cementing_set::deferred_size () const
{
	std::lock_guard lock{ mutex };
	return deferred.size ();
}

void lumex::cementing_set::run ()
{
	std::unique_lock lock{ mutex };
	while (!stopped)
	{
		stats.inc (lumex::stat::type::cementing_set, lumex::stat::detail::loop);

		cleanup (lock);
		debug_assert (lock.owns_lock ());

		if (!set.empty ())
		{
			// Only log if component is under pressure
			if ((set.size () + deferred.size ()) > lumex::queue_warning_threshold () && log_interval.elapse (15s))
			{
				logger.info (lumex::log::type::cementing_set, "{} blocks in cementing set, {} deferred",
				set.size (),
				deferred.size ());
			}

			run_batch (lock);
			debug_assert (!lock.owns_lock ());
			lock.lock ();
		}
		else
		{
			condition.wait (lock, [&] () { return !set.empty () || stopped; });
		}
	}
}

auto lumex::cementing_set::next_batch (size_t max_count) -> std::deque<entry>
{
	debug_assert (!mutex.try_lock ());
	debug_assert (!set.empty ());

	std::deque<entry> results;
	while (!set.empty () && results.size () < max_count)
	{
		results.push_back (set.front ());
		set.pop_front ();
	}
	return results;
}

void lumex::cementing_set::run_batch (std::unique_lock<std::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());
	debug_assert (!set.empty ());

	std::deque<context> cemented;
	std::deque<lumex::block_hash> already;

	auto batch = next_batch (config.batch_size);

	// Keep track of the blocks we're currently cementing, so that the .contains (...) check is accurate
	debug_assert (current.empty ());
	for (auto const & entry : batch)
	{
		current.insert (entry.hash);
	}

	lock.unlock ();

	auto notify = [this, &cemented] () {
		std::deque<context> batch;
		batch.swap (cemented);

		std::unique_lock lock{ mutex };

		// It's possible that ledger cementing happens faster than the notifications can be processed by other components, cooldown here
		while (workers.queued_tasks () >= config.max_queued_notifications)
		{
			stats.inc (lumex::stat::type::cementing_set, lumex::stat::detail::cooldown);
			condition.wait_for (lock, 100ms, [this] { return stopped.load (); });
			if (stopped)
			{
				return;
			}
		}

		workers.post ([this, batch = std::move (batch)] () {
			stats.inc (lumex::stat::type::cementing_set, lumex::stat::detail::notify);
			batch_cemented.notify (batch);
		});
	};

	// We might need to issue multiple notifications if the block we're confirming implicitly confirms more
	auto notify_maybe = [this, &cemented, &already, &notify] (auto & transaction) {
		if (cemented.size () >= config.max_blocks)
		{
			stats.inc (lumex::stat::type::cementing_set, lumex::stat::detail::notify_intermediate);
			transaction.commit ();
			notify ();
			transaction.renew ();
		}
	};

	{
		auto transaction = ledger.tx_begin_write (lumex::store::writer::confirmation_height);
		for (auto const & entry : batch)
		{
			auto const & hash = entry.hash;
			auto const & election = entry.election;

			size_t cemented_count = 0;
			bool success = false;
			do
			{
				transaction.refresh_if_needed ();

				// Cementing deep dependency chains might take a long time, allow for graceful shutdown, ignore notifications
				if (stopped)
				{
					return;
				}

				// Issue notifications here, so that `cemented` set is not too large before we add more blocks
				notify_maybe (transaction);

				stats.inc (lumex::stat::type::cementing_set, lumex::stat::detail::cementing);

				// The block might be rolled back before it's fully cemented
				if (!ledger.any.block_exists (transaction, hash))
				{
					stats.inc (lumex::stat::type::cementing_set, lumex::stat::detail::missing_block);
					break;
				}

				auto added = ledger.cement (transaction, hash, config.max_blocks);
				if (!added.empty ())
				{
					// Confirming this block may implicitly confirm more
					stats.add (lumex::stat::type::cementing_set, lumex::stat::detail::cemented, added.size ());
					for (auto & block : added)
					{
						cemented.push_back ({ block, hash, election });
					}
					cemented_count += added.size ();
				}
				else if (ledger.cemented.block_exists (transaction, hash))
				{
					stats.inc (lumex::stat::type::cementing_set, lumex::stat::detail::already_cemented);
					already.push_back (hash);
				}

				success = ledger.cemented.block_exists (transaction, hash);
			} while (!success);

			if (success)
			{
				stats.inc (lumex::stat::type::cementing_set, lumex::stat::detail::cemented_hash);
				logger.debug (lumex::log::type::cementing_set, "Cemented block: {} (total cemented: {})", hash, cemented_count);
			}
			else
			{
				stats.inc (lumex::stat::type::cementing_set, lumex::stat::detail::cementing_failed);
				logger.debug (lumex::log::type::cementing_set, "Failed to cement block: {}", hash);

				// Requeue failed blocks for processing later
				// Add them to the deferred set while still holding the exclusive database write transaction to avoid block processor races
				lock.lock ();
				deferred.push_back (entry);
				lock.unlock ();
			}
		}
	}

	notify ();
	release_assert (cemented.empty ());

	already_cemented.notify (already);

	// Clear current set only after the transaction is committed
	lock.lock ();
	current.clear ();
	lock.unlock ();
}

void lumex::cementing_set::cleanup (std::unique_lock<std::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());

	auto const cutoff = std::chrono::steady_clock::now () - config.deferred_age_cutoff;
	std::deque<entry> evicted;

	auto should_evict = [&] (entry const & entry) {
		return entry.timestamp < cutoff;
	};

	// Iterate in sequenced (insertion) order
	for (auto it = deferred.begin (), end = deferred.end (); it != end;)
	{
		if (should_evict (*it) || deferred.size () > config.max_deferred)
		{
			stats.inc (lumex::stat::type::cementing_set, lumex::stat::detail::evicted);
			evicted.push_back (*it);
			it = deferred.erase (it);
		}
		else
		{
			break; // Entries are sequenced, so we can stop here and avoid unnecessary iteration
		}
	}

	// Notify about evicted blocks so that other components can perform necessary cleanup
	if (!evicted.empty ())
	{
		lock.unlock ();
		for (auto const & entry : evicted)
		{
			cementing_failed.notify (entry.hash);
		}
		lock.lock ();
	}
}

lumex::container_info lumex::cementing_set::container_info () const
{
	std::lock_guard guard{ mutex };

	lumex::container_info info;
	info.put ("set", set);
	info.put ("deferred", deferred);
	info.add ("workers", workers.container_info ());
	return info;
}

/*
 * cementing_set_config
 */

lumex::error lumex::cementing_set_config::serialize (lumex::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Enable or disable cementing set.\ntype:bool");
	toml.put ("batch_size", batch_size, "Number of blocks to cement in a single batch.\ntype:uint64");
	toml.put ("max_blocks", max_blocks, "Maximum number of dependent blocks to be stored in memory during processing.\ntype:uint64");
	toml.put ("max_queued_notifications", max_queued_notifications, "Maximum number of notification batches to queue.\ntype:uint64");
	toml.put ("max_deferred", max_deferred, "Maximum number of failed blocks to keep for requeuing.\ntype:uint64");
	toml.put ("deferred_age_cutoff", deferred_age_cutoff.count (), "Max age of deferred blocks before they are dropped.\ntype:seconds");

	return toml.get_error ();
}

lumex::error lumex::cementing_set_config::deserialize (lumex::tomlconfig & toml)
{
	toml.get ("enable", enable);
	toml.get ("batch_size", batch_size);
	toml.get ("max_blocks", max_blocks);
	toml.get ("max_queued_notifications", max_queued_notifications);
	toml.get ("max_deferred", max_deferred);
	toml.get_duration ("deferred_age_cutoff", deferred_age_cutoff);

	return toml.get_error ();
}
