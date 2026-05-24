#include <lumex/lib/block_type.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/config.hpp>
#include <lumex/lib/enum_util.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/threading.hpp>
#include <lumex/lib/timer.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/block_processor.hpp>
#include <lumex/node/ledger_notifications.hpp>
#include <lumex/node/local_vote_history.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/transport/formatting.hpp>
#include <lumex/node/unchecked_map.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_set_any.hpp>

#include <utility>

/*
 * block_processor
 */

lumex::block_processor::block_processor (lumex::node_config const & node_config_a, lumex::ledger & ledger_a, lumex::ledger_notifications & ledger_notifications_a, lumex::unchecked_map & unchecked_a, lumex::stats & stats_a, lumex::logger & logger_a) :
	config{ *node_config_a.block_processor },
	node_config{ node_config_a },
	network_params{ node_config.network_params },
	ledger{ ledger_a },
	ledger_notifications{ ledger_notifications_a },
	unchecked{ unchecked_a },
	stats{ stats_a },
	logger{ logger_a }
{
	queue.max_size_query = [this] (auto const & origin) {
		switch (origin.source)
		{
			case lumex::block_source::live:
			case lumex::block_source::live_originator:
				return config.max_peer_queue;
			default:
				return config.max_system_queue;
		}
	};

	queue.priority_query = [this] (auto const & origin) -> size_t {
		switch (origin.source)
		{
			case lumex::block_source::live:
			case lumex::block_source::live_originator:
				return config.priority_live;
			case lumex::block_source::bootstrap:
			case lumex::block_source::bootstrap_legacy:
				return config.priority_bootstrap;
			case lumex::block_source::local:
				return config.priority_local;
			default:
				return config.priority_system;
		}
	};

	// Requeue blocks that could not be immediately processed
	unchecked.satisfied.add ([this] (lumex::unchecked_info const & info) {
		add (info.block, lumex::block_source::unchecked);
	});
}

lumex::block_processor::~block_processor ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void lumex::block_processor::start ()
{
	debug_assert (!thread.joinable ());

	boost::thread::attributes attrs;
	attrs.set_stack_size (lumex::ledger_thread_stack_size ());

	thread = boost::thread (attrs, [this] () {
		lumex::thread_role::set (lumex::thread_role::name::block_processing);
		run ();
	});
}

void lumex::block_processor::stop ()
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
}

// TODO: Remove and replace all checks with calls to size (block_source)
std::size_t lumex::block_processor::size () const
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	return queue.size ();
}

std::size_t lumex::block_processor::size (lumex::block_source source) const
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	return queue.size ({ source });
}

bool lumex::block_processor::add (std::shared_ptr<lumex::block> const & block, block_source const source, std::shared_ptr<lumex::transport::channel> const & channel, std::function<void (lumex::block_status)> callback)
{
	if (network_params.work.validate_entry (*block)) // true => error
	{
		stats.inc (lumex::stat::type::block_processor, lumex::stat::detail::insufficient_work);
		return false; // Not added
	}

	logger.debug (lumex::log::type::block_processor, "Processing block (async): {} (source: {} {})", block->hash (), source, channel);

	bool added = add_impl ({ block, source, std::move (callback) }, channel);
	if (added)
	{
		stats.inc (lumex::stat::type::block_processor, lumex::stat::detail::process);
	}
	return added;
}

std::size_t lumex::block_processor::add_many (std::deque<std::shared_ptr<lumex::block>> const & blocks, block_source const source, std::shared_ptr<lumex::transport::channel> const & channel, std::function<void (lumex::block_status)> last_callback)
{
	if (blocks.empty ())
	{
		return 0;
	}

	// Validate work outside the lock, build context objects
	std::deque<lumex::block_context> contexts;
	std::size_t insufficient_work_count = 0;

	for (auto const & block : blocks)
	{
		if (network_params.work.validate_entry (*block)) // true => error
		{
			++insufficient_work_count;
		}
		else
		{
			contexts.emplace_back (block, source);
		}
	}

	if (insufficient_work_count > 0)
	{
		stats.add (lumex::stat::type::block_processor, lumex::stat::detail::insufficient_work, insufficient_work_count);
	}

	if (contexts.empty ())
	{
		return 0;
	}

	// Attach callback to last valid block
	if (last_callback)
	{
		contexts.back ().callback = std::move (last_callback);
	}

	// Push all contexts under a single lock
	std::size_t added = 0;
	std::size_t overfill = 0;
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		for (auto & ctx : contexts)
		{
			if (queue.push (std::move (ctx), { source, channel }))
			{
				++added;
			}
			else
			{
				++overfill;
			}
		}
	}

	if (added > 0)
	{
		stats.add (lumex::stat::type::block_processor, lumex::stat::detail::process, added);
		condition.notify_all ();
	}
	if (overfill > 0)
	{
		stats.add (lumex::stat::type::block_processor, lumex::stat::detail::overfill, overfill);
		stats.add (lumex::stat::type::block_processor_overfill, to_stat_detail (source), overfill);
	}

	logger.debug (lumex::log::type::block_processor, "Processing blocks (async batch): total={}, added={}, overfill={}, invalid_work={} (source: {} {})",
	blocks.size (), added, overfill, insufficient_work_count, source, channel);

	return added;
}

std::optional<lumex::block_status> lumex::block_processor::add_blocking (std::shared_ptr<lumex::block> const & block, block_source const source)
{
	logger.debug (lumex::log::type::block_processor, "Processing block (blocking): {} (source: {})", block->hash (), source);

	lumex::block_context ctx{ block, source };
	auto future = ctx.get_future ();
	bool added = add_impl (std::move (ctx));
	if (added)
	{
		stats.inc (lumex::stat::type::block_processor, lumex::stat::detail::process_blocking);
	}

	try
	{
		future.wait ();
		return future.get ();
	}
	catch (std::future_error const &)
	{
		stats.inc (lumex::stat::type::block_processor, lumex::stat::detail::process_blocking_timeout);
		logger.error (lumex::log::type::block_processor, "Block dropped when processing: {}", block->hash ());
	}

	return std::nullopt;
}

void lumex::block_processor::force (std::shared_ptr<lumex::block> const & block)
{
	logger.debug (lumex::log::type::block_processor, "Forcing block: {}", block->hash ());

	bool added = add_impl ({ block, block_source::forced });
	if (added)
	{
		stats.inc (lumex::stat::type::block_processor, lumex::stat::detail::force);
	}
	else
	{
		stats.inc (lumex::stat::type::block_processor, lumex::stat::detail::force_overfill);
		logger.debug (lumex::log::type::block_processor, "Failed to force block (queue full): {}", block->hash ());
	}
}

bool lumex::block_processor::add_impl (lumex::block_context ctx, std::shared_ptr<lumex::transport::channel> const & channel)
{
	auto const source = ctx.source;
	bool added = false;
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		added = queue.push (std::move (ctx), { source, channel });
	}
	if (added)
	{
		condition.notify_all ();
	}
	else
	{
		stats.inc (lumex::stat::type::block_processor, lumex::stat::detail::overfill);
		stats.inc (lumex::stat::type::block_processor_overfill, to_stat_detail (source));
	}
	return added;
}

void lumex::block_processor::rollback_competitor (secure::write_transaction & transaction, lumex::block const & fork_block)
{
	auto const hash = fork_block.hash ();
	auto const successor_hash = ledger.any.block_successor (transaction, fork_block.qualified_root ());
	auto const successor = successor_hash ? ledger.any.block_get (transaction, successor_hash.value ()) : nullptr;
	if (successor != nullptr && successor->hash () != hash)
	{
		// Replace our block with the winner and roll back any dependent blocks
		logger.debug (lumex::log::type::block_processor, "Rolling back: {} and replacing with: {}", successor->hash (), hash);

		std::deque<std::shared_ptr<lumex::block>> rollback_list;
		bool error = ledger.rollback (transaction, successor->hash (), rollback_list);
		if (error)
		{
			stats.inc (lumex::stat::type::ledger, lumex::stat::detail::rollback_failed);
			logger.warn (lumex::log::type::block_processor, "Failed to roll back: {} (succeeded with {} dependents)", successor->hash (), rollback_list.size ());
		}
		else
		{
			stats.inc (lumex::stat::type::ledger, lumex::stat::detail::rollback);
			logger.debug (lumex::log::type::block_processor, "Rolled back {} with {} dependents", successor->hash (), rollback_list.size ());
		}

		if (!rollback_list.empty ())
		{
			// Notify observers of the rolled back blocks on a background thread while not holding the ledger write lock
			ledger_notifications.notify_rolled_back (transaction, std::move (rollback_list), fork_block.qualified_root (), [this] {
				stats.inc (lumex::stat::type::block_processor, lumex::stat::detail::notify_rolled_back);
			});
		}
	}
}

double lumex::block_processor::backlog_factor () const
{
	auto const backlog = ledger.backlog_size ();
	auto const max_backlog = ledger.max_backlog ();
	if (max_backlog == 0 || backlog <= max_backlog * config.backlog_threshold)
	{
		return 0.0;
	}
	return std::max (1.0, static_cast<double> (backlog) / static_cast<double> (max_backlog * config.backlog_threshold));
}

void lumex::block_processor::wait_backlog (lumex::unique_lock<lumex::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());

	double const factor = backlog_factor ();

	if (factor < 1.0)
	{
		return;
	}

	auto scaling = [] (double factor) {
		// This uses a power of approximately 3.32, which gives ~1x at 1.0 and ~10x at 2.0
		return std::pow (factor, 3.32);
	};
	auto const throttle_wait = std::min (config.backlog_throttle * scaling (factor), config.backlog_throttle_max * 1.0);

	if (log_backlog_interval.elapse (15s))
	{
		logger.warn (lumex::log::type::block_processor, "Backlog exceeded, throttling for {}ms (backlog factor: {})",
		throttle_wait.count (),
		factor);
	}

	stats.inc (lumex::stat::type::block_processor, lumex::stat::detail::cooldown_backlog);

	condition.wait_for (lock, throttle_wait, [&] {
		return stopped || backlog_factor () < 1.0;
	});
}

void lumex::block_processor::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait (lock, [this] {
			return stopped || !queue.empty ();
		});

		if (stopped)
		{
			return;
		}

		if (config.enable_throttling)
		{
			wait_backlog (lock);
			debug_assert (lock.owns_lock ());
		}

		lock.unlock ();

		// It's possible that ledger processing happens faster than the notifications can be processed by other components, cooldown here
		ledger_notifications.wait ([this] {
			stats.inc (lumex::stat::type::block_processor, lumex::stat::detail::cooldown);
			if (log_cooldown_interval.elapse (lumex::is_dev_run () ? 1s : 15s))
			{
				logger.warn (lumex::log::type::block_processor, "Cooldown in block processing, waiting for remaining ledger notifications to be processed");
			}
		});

		lock.lock ();

		if (!queue.empty ())
		{
			// Only log if component is under pressure
			if (queue.size () > lumex::queue_warning_threshold () && log_processing_interval.elapse (15s))
			{
				logger.info (lumex::log::type::block_processor, "{} blocks ({} forced) in processing queue",
				queue.size (),
				queue.size ({ lumex::block_source::forced }));
			}

			process_batch (lock);
			debug_assert (!lock.owns_lock ());
			lock.lock ();
		}
	}
}

auto lumex::block_processor::next () -> lumex::block_context
{
	debug_assert (!mutex.try_lock ());
	debug_assert (!queue.empty ()); // This should be checked before calling next

	if (!queue.empty ())
	{
		auto [request, origin] = queue.next ();
		release_assert (origin.source != lumex::block_source::forced || request.source == lumex::block_source::forced);
		return std::move (request);
	}

	release_assert (false, "next() called when no blocks are ready");
}

auto lumex::block_processor::next_batch (size_t max_count) -> std::deque<lumex::block_context>
{
	debug_assert (!mutex.try_lock ());
	debug_assert (!queue.empty ());

	queue.periodic_update ();

	std::deque<lumex::block_context> results;
	while (!queue.empty () && results.size () < max_count)
	{
		results.push_back (next ());
	}
	return results;
}

void lumex::block_processor::process_batch (lumex::unique_lock<lumex::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());
	debug_assert (!queue.empty ());

	auto batch = next_batch (config.batch_size);

	lock.unlock ();

	auto transaction = ledger.tx_begin_write (lumex::store::writer::block_processor);

	lumex::timer<std::chrono::milliseconds> timer;
	timer.start ();

	// Processing blocks
	size_t number_of_blocks_processed = 0;
	size_t number_of_forced_processed = 0;

	std::deque<std::pair<lumex::block_status, lumex::block_context>> processed;

	for (auto & ctx : batch)
	{
		auto const hash = ctx.block->hash ();
		bool const force = ctx.source == lumex::block_source::forced;

		transaction.refresh_if_needed ();

		if (force)
		{
			number_of_forced_processed++;
			rollback_competitor (transaction, *ctx.block);
		}

		number_of_blocks_processed++;

		auto result = process_one (transaction, ctx, force);
		processed.emplace_back (result, std::move (ctx));
	}

	// We had rocksdb issues in the past, ensure that rep weights are always consistent
	ledger.verify_consistency (transaction);

	if (number_of_blocks_processed != 0 && timer.stop () > std::chrono::milliseconds (100))
	{
		logger.debug (lumex::log::type::block_processor, "Processed {} blocks ({} forced) in {} {}",
		number_of_blocks_processed,
		number_of_forced_processed,
		timer.value ().count (), timer.unit ());
	}

	// Queue notifications to be dispatched in the background
	ledger_notifications.notify_processed (transaction, std::move (processed), [this] {
		stats.inc (lumex::stat::type::block_processor, lumex::stat::detail::notify_processed);
	});
}

lumex::block_status lumex::block_processor::process_one (secure::write_transaction const & transaction_a, lumex::block_context const & context, bool const forced_a)
{
	auto block = context.block;
	auto const hash = block->hash ();
	lumex::block_status result = ledger.process (transaction_a, block);

	stats.inc (lumex::stat::type::block_processor_result, to_stat_detail (result));
	stats.inc (lumex::stat::type::block_processor_source, to_stat_detail (context.source));

	logger.trace (lumex::log::type::block_processor, lumex::log::detail::block_processed,
	lumex::log::arg{ "result", result },
	lumex::log::arg{ "source", context.source },
	lumex::log::arg{ "arrival", lumex::log::microseconds (context.arrival) },
	lumex::log::arg{ "forced", forced_a },
	lumex::log::arg{ "block", block });

	switch (result)
	{
		case lumex::block_status::progress:
		{
			unchecked.trigger (hash);

			/*
			 * For send blocks check epoch open unchecked (gap pending).
			 * For state blocks check only send subtype and only if block epoch is not last epoch.
			 * If epoch is last, then pending entry shouldn't trigger same epoch open block for destination account.
			 */
			if (block->type () == lumex::block_type::send || (block->type () == lumex::block_type::state && block->is_send () && std::underlying_type_t<lumex::epoch> (block->sideband ().details.epoch) < std::underlying_type_t<lumex::epoch> (lumex::epoch::max)))
			{
				unchecked.trigger (block->destination ());
			}
			break;
		}
		case lumex::block_status::gap_previous:
		{
			unchecked.put (block->previous (), block);
			stats.inc (lumex::stat::type::ledger, lumex::stat::detail::gap_previous);
			break;
		}
		case lumex::block_status::gap_source:
		{
			release_assert (block->source_field () || block->link_field ());
			unchecked.put (block->source_field ().value_or (block->link_field ().value_or (0).as_block_hash ()), block);
			stats.inc (lumex::stat::type::ledger, lumex::stat::detail::gap_source);
			break;
		}
		case lumex::block_status::gap_epoch_open_pending:
		{
			unchecked.put (block->account_field ().value_or (0), block); // Specific unchecked key starting with epoch open block account public key
			stats.inc (lumex::stat::type::ledger, lumex::stat::detail::gap_source);
			break;
		}
		case lumex::block_status::old:
		{
			stats.inc (lumex::stat::type::ledger, lumex::stat::detail::old);
			break;
		}
		// These are unexpected and indicate erroneous/malicious behavior, log debug info to highlight the issue
		case lumex::block_status::bad_signature:
		{
			logger.debug (lumex::log::type::block_processor, "Block signature is invalid: {}", hash);
			break;
		}
		case lumex::block_status::negative_spend:
		{
			logger.debug (lumex::log::type::block_processor, "Block spends negative amount: {}", hash);
			break;
		}
		case lumex::block_status::unreceivable:
		{
			logger.debug (lumex::log::type::block_processor, "Block is unreceivable: {}", hash);
			break;
		}
		case lumex::block_status::fork:
		{
			stats.inc (lumex::stat::type::ledger, lumex::stat::detail::fork);
			logger.debug (lumex::log::type::block_processor, "Block is a fork: {}", hash);
			break;
		}
		case lumex::block_status::opened_burn_account:
		{
			logger.debug (lumex::log::type::block_processor, "Block opens burn account: {}", hash);
			break;
		}
		case lumex::block_status::balance_mismatch:
		{
			logger.debug (lumex::log::type::block_processor, "Block balance mismatch: {}", hash);
			break;
		}
		case lumex::block_status::representative_mismatch:
		{
			logger.debug (lumex::log::type::block_processor, "Block representative mismatch: {}", hash);
			break;
		}
		case lumex::block_status::block_position:
		{
			logger.debug (lumex::log::type::block_processor, "Block is in incorrect position: {}", hash);
			break;
		}
		case lumex::block_status::insufficient_work:
		{
			logger.debug (lumex::log::type::block_processor, "Block has insufficient work: {}", hash);
			break;
		}
		case lumex::block_status::invalid:
		{
			debug_assert (false, "invalid block status"); // This should never happen
			break;
		}
	}
	return result;
}

lumex::container_info lumex::block_processor::container_info () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	lumex::container_info info;
	info.put ("blocks", queue.size ());
	info.put ("forced", queue.size ({ lumex::block_source::forced }));
	info.add ("queue", queue.container_info ());
	return info;
}

/*
 * block_processor_config
 */

lumex::block_processor_config::block_processor_config (const lumex::network_constants & network_constants)
{
}

lumex::error lumex::block_processor_config::serialize (lumex::tomlconfig & toml) const
{
	toml.put ("max_peer_queue", max_peer_queue, "Maximum number of blocks to queue from network peers. \ntype:uint64");
	toml.put ("max_system_queue", max_system_queue, "Maximum number of blocks to queue from system components (local RPC, bootstrap). \ntype:uint64");
	toml.put ("priority_live", priority_live, "Priority for live network blocks. Higher priority gets processed more frequently. \ntype:uint64");
	toml.put ("priority_bootstrap", priority_bootstrap, "Priority for bootstrap blocks. Higher priority gets processed more frequently. \ntype:uint64");
	toml.put ("priority_local", priority_local, "Priority for local RPC blocks. Higher priority gets processed more frequently. \ntype:uint64");
	toml.put ("enable_throttling", enable_throttling, "Enable throttling of block processing when backlog exceeds threshold. \ntype:bool");
	toml.put ("backlog_threshold", backlog_threshold, "Threshold for backlog before throttling is applied. \ntype:double");
	toml.put ("backlog_throttle", backlog_throttle.count (), "Throttling interval for processing blocks when backlog is above threshold. \ntype:milliseconds");
	toml.put ("backlog_throttle_max", backlog_throttle_max.count (), "Maximum throttling interval for processing blocks when backlog is above threshold. \ntype:milliseconds");

	return toml.get_error ();
}

lumex::error lumex::block_processor_config::deserialize (lumex::tomlconfig & toml)
{
	toml.get ("max_peer_queue", max_peer_queue);
	toml.get ("max_system_queue", max_system_queue);
	toml.get ("priority_live", priority_live);
	toml.get ("priority_bootstrap", priority_bootstrap);
	toml.get ("priority_local", priority_local);
	toml.get ("enable_throttling", enable_throttling);
	toml.get ("backlog_threshold", backlog_threshold);
	toml.get_duration ("backlog_throttle", backlog_throttle);
	toml.get_duration ("backlog_throttle_max", backlog_throttle_max);

	return toml.get_error ();
}
