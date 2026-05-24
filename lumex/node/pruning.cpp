#include <lumex/lib/logging.hpp>
#include <lumex/lib/saturate.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/thread_roles.hpp>
#include <lumex/lib/threading.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/pruning.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_set_any.hpp>
#include <lumex/store/ledger/confirmation_height.hpp>

lumex::pruning::pruning (lumex::node_config const & config_a, lumex::node_flags const & flags_a, lumex::ledger & ledger_a, lumex::stats & stats_a, lumex::logger & logger_a) :
	config{ config_a },
	flags{ flags_a },
	ledger{ ledger_a },
	stats{ stats_a },
	logger{ logger_a }
{
}

lumex::pruning::~pruning ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void lumex::pruning::start ()
{
	debug_assert (!thread.joinable ());

	if (!flags.enable_pruning)
	{
		return;
	}

	thread = std::thread{ [this] () {
		lumex::thread_role::set (lumex::thread_role::name::pruning);
		run ();
	} };
}

void lumex::pruning::stop ()
{
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	lumex::join_or_pass (thread);
}

void lumex::pruning::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		lock.unlock ();

		bool bootstrap_height_reached = ledger.bootstrap_height_reached ();

		ledger_pruning (flags.block_processor_batch_size != 0 ? flags.block_processor_batch_size : 2 * 1024, bootstrap_height_reached);

		auto const ledger_pruning_interval (bootstrap_height_reached ? config.max_pruning_age : std::min (config.max_pruning_age, std::chrono::seconds (15 * 60)));
		logger.debug (lumex::log::type::pruning, "Next pruning iteration in {}s", ledger_pruning_interval.count ());

		lock.lock ();

		condition.wait_for (lock, ledger_pruning_interval, [this] () {
			return stopped;
		});
	}
}

void lumex::pruning::ledger_pruning (uint64_t const batch_size_a, bool bootstrap_weight_reached_a)
{
	stats.inc (lumex::stat::type::pruning, lumex::stat::detail::ledger_pruning);

	uint64_t const max_depth (config.max_pruning_depth != 0 ? config.max_pruning_depth : std::numeric_limits<uint64_t>::max ());
	uint64_t const cutoff_time (bootstrap_weight_reached_a ? lumex::seconds_since_epoch () - config.max_pruning_age.count () : std::numeric_limits<uint64_t>::max ());
	uint64_t pruned_count (0);
	uint64_t transaction_write_count (0);
	lumex::account last_account (1); // 0 Burn account is never opened. So it can be used to break loop
	std::deque<lumex::block_hash> pruning_targets;
	bool target_finished (false);
	while ((transaction_write_count != 0 || !target_finished) && !stopped)
	{
		// Search pruning targets
		while (pruning_targets.size () < batch_size_a && !target_finished && !stopped)
		{
			stats.inc (lumex::stat::type::pruning, lumex::stat::detail::collect_targets);
			target_finished = collect_ledger_pruning_targets (pruning_targets, last_account, batch_size_a * 2, max_depth, cutoff_time);
		}
		// Pruning write operation
		transaction_write_count = 0;
		if (!pruning_targets.empty () && !stopped)
		{
			auto write_transaction = ledger.tx_begin_write (lumex::store::writer::pruning);
			while (!pruning_targets.empty () && transaction_write_count < batch_size_a && !stopped)
			{
				stats.inc (lumex::stat::type::pruning, lumex::stat::detail::pruning_target);

				auto const & pruning_hash (pruning_targets.front ());
				auto account_pruned_count (ledger.pruning_action (write_transaction, pruning_hash, batch_size_a));
				transaction_write_count += account_pruned_count;
				pruning_targets.pop_front ();

				stats.add (lumex::stat::type::pruning, lumex::stat::detail::pruned_count, account_pruned_count);
			}
			pruned_count += transaction_write_count;

			logger.debug (lumex::log::type::pruning, "Pruned blocks: {}", pruned_count);
		}
	}

	logger.info (lumex::log::type::pruning, "Recently pruned blocks: {}", pruned_count);
}

bool lumex::pruning::collect_ledger_pruning_targets (std::deque<lumex::block_hash> & pruning_targets_a, lumex::account & last_account_a, uint64_t const batch_read_size_a, uint64_t const max_depth_a, uint64_t const cutoff_time_a)
{
	uint64_t read_operations (0);
	bool finish_transaction (false);
	auto transaction = ledger.tx_begin_read ();
	for (auto i (ledger.store.confirmation_height.begin (transaction, last_account_a)), n (ledger.store.confirmation_height.end (transaction)); i != n && !finish_transaction;)
	{
		++read_operations;
		auto const & account (i->first);
		lumex::block_hash hash (i->second.frontier);
		uint64_t depth (0);
		while (!hash.is_zero () && depth < max_depth_a)
		{
			auto block = ledger.any.block_get (transaction, hash);
			if (block != nullptr)
			{
				if (block->sideband ().timestamp > cutoff_time_a || depth == 0)
				{
					hash = block->previous ();
				}
				else
				{
					break;
				}
			}
			else
			{
				release_assert (depth != 0);
				hash = 0;
			}
			++depth;
		}
		if (!hash.is_zero ())
		{
			pruning_targets_a.push_back (hash);
		}
		read_operations += depth;
		if (read_operations >= batch_read_size_a)
		{
			last_account_a = inc_sat (account.number ());
			finish_transaction = true;
		}
		else
		{
			++i;
		}
	}
	return !finish_transaction || last_account_a.is_zero ();
}

lumex::container_info lumex::pruning::container_info () const
{
	lumex::container_info info;
	return info;
}