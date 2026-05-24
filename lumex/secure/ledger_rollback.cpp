#include <lumex/lib/blocks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_rollback.hpp>
#include <lumex/secure/ledger_set_any.hpp>
#include <lumex/secure/rep_weights.hpp>
#include <lumex/store/ledger/account.hpp>
#include <lumex/store/ledger/block.hpp>
#include <lumex/store/ledger/pending.hpp>
#include <lumex/store/ledger/successor.hpp>
#include <lumex/store/ledger/topology.hpp>
#include <lumex/store/ledger_store.hpp>

lumex::ledger_rollback::ledger_rollback (lumex::secure::write_transaction const & transaction_a, lumex::ledger & ledger_a, std::deque<std::shared_ptr<lumex::block>> & list_a, size_t depth_a, size_t max_depth_a) :
	transaction (transaction_a),
	ledger (ledger_a),
	list (list_a),
	depth (depth_a),
	max_depth (max_depth_a)
{
}

void lumex::ledger_rollback::send_block (lumex::send_block const & block_a)
{
	auto hash (block_a.hash ());
	lumex::pending_key key (block_a.hashables.destination, hash);
	auto pending = ledger.store.pending.get (transaction, key);
	while (!error && !pending.has_value ())
	{
		error = ledger.rollback (transaction, ledger.any.account_head (transaction, block_a.hashables.destination), list, depth + 1, max_depth);
		pending = ledger.store.pending.get (transaction, key);
	}
	if (!error)
	{
		auto info = ledger.any.account_get (transaction, pending.value ().source);
		release_assert (info);
		ledger.store.pending.del (transaction, key);
		ledger.rep_weights.add (transaction, info->representative, pending.value ().amount);
		lumex::account_info new_info (block_a.hashables.previous, info->representative, info->open_block, ledger.any.block_balance (transaction, block_a.hashables.previous).value (), lumex::seconds_since_epoch (), info->block_count - 1, lumex::epoch::epoch_0);
		ledger.update_account (transaction, pending.value ().source, *info, new_info);
		ledger.store.block.del (transaction, hash);
		ledger.store.successor.del (transaction, block_a.hashables.previous);
		if (block_a.sideband ().topo_height != 0)
		{
			ledger.store.topology.del (transaction, { block_a.sideband ().topo_height, hash });
		}
		ledger.stats.inc (lumex::stat::type::rollback, lumex::stat::detail::send);
	}
}

void lumex::ledger_rollback::receive_block (lumex::receive_block const & block_a)
{
	auto hash (block_a.hash ());
	auto amount = ledger.any.block_amount (transaction, hash).value ().number ();
	auto destination_account = block_a.account ();
	// Pending account entry can be incorrect if source block was pruned. But it's not affecting correct ledger processing
	auto source_account = ledger.any.block_account (transaction, block_a.hashables.source);
	auto info = ledger.any.account_get (transaction, destination_account);
	release_assert (info);
	ledger.rep_weights.sub (transaction, info->representative, amount);
	lumex::account_info new_info (block_a.hashables.previous, info->representative, info->open_block, ledger.any.block_balance (transaction, block_a.hashables.previous).value (), lumex::seconds_since_epoch (), info->block_count - 1, lumex::epoch::epoch_0);
	ledger.update_account (transaction, destination_account, *info, new_info);
	ledger.store.block.del (transaction, hash);
	ledger.store.pending.put (transaction, lumex::pending_key (destination_account, block_a.hashables.source), { source_account.value_or (0), amount, lumex::epoch::epoch_0 });
	ledger.store.successor.del (transaction, block_a.hashables.previous);
	if (block_a.sideband ().topo_height != 0)
	{
		ledger.store.topology.del (transaction, { block_a.sideband ().topo_height, hash });
	}
	ledger.stats.inc (lumex::stat::type::rollback, lumex::stat::detail::receive);
}

void lumex::ledger_rollback::open_block (lumex::open_block const & block_a)
{
	auto hash (block_a.hash ());
	auto amount = ledger.any.block_amount (transaction, hash).value ().number ();
	auto destination_account = block_a.account ();
	auto source_account = ledger.any.block_account (transaction, block_a.hashables.source);
	ledger.rep_weights.sub (transaction, block_a.representative_field ().value (), amount);
	lumex::account_info new_info;
	ledger.update_account (transaction, destination_account, new_info, new_info);
	ledger.store.block.del (transaction, hash);
	ledger.store.pending.put (transaction, lumex::pending_key (destination_account, block_a.hashables.source), { source_account.value_or (0), amount, lumex::epoch::epoch_0 });
	if (block_a.sideband ().topo_height != 0)
	{
		ledger.store.topology.del (transaction, { block_a.sideband ().topo_height, hash });
	}
	ledger.stats.inc (lumex::stat::type::rollback, lumex::stat::detail::open);
}

void lumex::ledger_rollback::change_block (lumex::change_block const & block_a)
{
	auto hash (block_a.hash ());
	auto rep_block_hash (ledger.representative_block (transaction, block_a.hashables.previous));
	auto account = block_a.account ();
	auto info = ledger.any.account_get (transaction, account);
	release_assert (info);
	auto balance = ledger.any.block_balance (transaction, block_a.hashables.previous).value ();
	auto rep_block = ledger.store.block.get (transaction, rep_block_hash);
	release_assert (rep_block != nullptr);
	auto representative = rep_block->representative_field ().value ();
	ledger.rep_weights.move (transaction, block_a.hashables.representative, representative, balance);
	ledger.store.block.del (transaction, hash);
	lumex::account_info new_info (block_a.hashables.previous, representative, info->open_block, info->balance, lumex::seconds_since_epoch (), info->block_count - 1, lumex::epoch::epoch_0);
	ledger.update_account (transaction, account, *info, new_info);
	ledger.store.successor.del (transaction, block_a.hashables.previous);
	if (block_a.sideband ().topo_height != 0)
	{
		ledger.store.topology.del (transaction, { block_a.sideband ().topo_height, hash });
	}
	ledger.stats.inc (lumex::stat::type::rollback, lumex::stat::detail::change);
}

void lumex::ledger_rollback::state_block (lumex::state_block const & block_a)
{
	auto const hash = block_a.hash ();
	auto const previous_balance = ledger.any.block_balance (transaction, block_a.hashables.previous).value_or (0).number ();
	bool const is_send = block_a.hashables.balance < previous_balance;

	auto info = ledger.any.account_get (transaction, block_a.hashables.account);
	release_assert (info);

	if (is_send)
	{
		lumex::pending_key key (block_a.hashables.link.as_account (), hash);
		while (!error && !ledger.any.pending_get (transaction, key))
		{
			error = ledger.rollback (transaction, ledger.any.account_head (transaction, block_a.hashables.link.as_account ()), list, depth + 1, max_depth);
		}
		if (error)
		{
			return;
		}
		ledger.store.pending.del (transaction, key);
		ledger.stats.inc (lumex::stat::type::rollback, lumex::stat::detail::send);
	}
	else if (!block_a.hashables.link.is_zero () && !ledger.is_epoch_link (block_a.hashables.link))
	{
		// Pending account entry can be incorrect if source block was pruned. But it's not affecting correct ledger processing
		auto source_account = ledger.any.block_account (transaction, block_a.hashables.link.as_block_hash ());
		lumex::pending_info pending_info (source_account.value_or (0), block_a.hashables.balance.number () - previous_balance, block_a.sideband ().source_epoch);
		ledger.store.pending.put (transaction, lumex::pending_key (block_a.hashables.account, block_a.hashables.link.as_block_hash ()), pending_info);
		ledger.stats.inc (lumex::stat::type::rollback, lumex::stat::detail::receive);
	}

	release_assert (!error);

	lumex::block_hash rep_block_hash (0);
	if (!block_a.hashables.previous.is_zero ())
	{
		rep_block_hash = ledger.representative_block (transaction, block_a.hashables.previous);
	}

	lumex::account previous_representative{};
	if (!rep_block_hash.is_zero ())
	{
		// Move existing representation & add in amount delta
		auto rep_block (ledger.store.block.get (transaction, rep_block_hash));
		release_assert (rep_block != nullptr);
		previous_representative = rep_block->representative_field ().value ();
		// ledger.rep_weights.representation_add_dual (transaction, representative, previous_balance, block_a.hashables.representative, 0 - block_a.hashables.balance.number ());
		ledger.rep_weights.move_add_sub (transaction, block_a.hashables.representative, block_a.hashables.balance, previous_representative, previous_balance);
	}
	else
	{
		// Add in amount delta only
		ledger.rep_weights.sub (transaction, block_a.hashables.representative, block_a.hashables.balance.number ());
	}

	auto previous_version (ledger.version (transaction, block_a.hashables.previous));
	lumex::account_info new_info (block_a.hashables.previous, previous_representative, info->open_block, previous_balance, lumex::seconds_since_epoch (), info->block_count - 1, previous_version);
	ledger.update_account (transaction, block_a.hashables.account, *info, new_info);

	if (!block_a.hashables.previous.is_zero ())
	{
		debug_assert (ledger.any.block_exists_or_pruned (transaction, block_a.hashables.previous));
		ledger.store.successor.del (transaction, block_a.hashables.previous);
	}
	else
	{
		ledger.stats.inc (lumex::stat::type::rollback, lumex::stat::detail::open);
	}

	ledger.store.block.del (transaction, hash);

	if (block_a.sideband ().topo_height != 0)
	{
		ledger.store.topology.del (transaction, { block_a.sideband ().topo_height, hash });
	}
}