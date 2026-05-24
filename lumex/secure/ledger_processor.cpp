#include <lumex/lib/blocks.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/lib/work.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_processor.hpp>
#include <lumex/secure/ledger_set_any.hpp>
#include <lumex/secure/network_params.hpp>
#include <lumex/secure/rep_weights.hpp>
#include <lumex/store/ledger/account.hpp>
#include <lumex/store/ledger/block.hpp>
#include <lumex/store/ledger/pending.hpp>
#include <lumex/store/ledger/topology.hpp>
#include <lumex/store/ledger_store.hpp>

#include <boost/multiprecision/cpp_int.hpp>

lumex::ledger_processor::ledger_processor (lumex::secure::write_transaction const & transaction_a, lumex::ledger & ledger_a) :
	transaction (transaction_a),
	ledger (ledger_a)
{
}

void lumex::ledger_processor::send_block (lumex::send_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing = ledger.any.block_exists_or_pruned (transaction, hash);
	result = existing ? lumex::block_status::old : lumex::block_status::progress; // Have we seen this block before? (Harmless)
	if (result == lumex::block_status::progress)
	{
		auto previous (ledger.store.block.get (transaction, block_a.hashables.previous));
		result = previous != nullptr ? lumex::block_status::progress : lumex::block_status::gap_previous; // Have we seen the previous block already? (Harmless)
		if (result == lumex::block_status::progress)
		{
			result = block_a.valid_predecessor (*previous) ? lumex::block_status::progress : lumex::block_status::block_position;
			if (result == lumex::block_status::progress)
			{
				auto account = previous->account ();
				auto info = ledger.any.account_get (transaction, account);
				release_assert (info);
				result = info->head != block_a.hashables.previous ? lumex::block_status::fork : lumex::block_status::progress;
				if (result == lumex::block_status::progress)
				{
					result = validate_message (account, hash, block_a.signature) ? lumex::block_status::bad_signature : lumex::block_status::progress; // Is this block signed correctly (Malformed)
					if (result == lumex::block_status::progress)
					{
						lumex::block_details block_details (lumex::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */);
						result = ledger.work.difficulty (block_a) >= ledger.work.threshold (block_a.work_version (), block_details) ? lumex::block_status::progress : lumex::block_status::insufficient_work; // Does this block have sufficient work? (Malformed)
						if (result == lumex::block_status::progress)
						{
							debug_assert (!validate_message (account, hash, block_a.signature));
							release_assert (info->head == block_a.hashables.previous);
							result = info->balance.number () >= block_a.hashables.balance.number () ? lumex::block_status::progress : lumex::block_status::negative_spend; // Is this trying to spend a negative amount (Malicious)
							if (result == lumex::block_status::progress)
							{
								auto amount (info->balance.number () - block_a.hashables.balance.number ());
								ledger.rep_weights.sub (transaction, info->representative, amount);
								auto const topo = topology_height (previous);
								block_a.sideband_set (lumex::block_sideband{
								/* account */ account,
								/* balance */ block_a.hashables.balance,
								/* height */ info->block_count + 1,
								/* timestamp */ lumex::seconds_since_epoch (),
								/* details */ block_details,
								/* source_epoch */ lumex::epoch::epoch_0,
								/* topo_height */ topo });
								ledger.store.block.put (transaction, hash, block_a);
								if (topo != 0)
								{
									ledger.store.topology.put (transaction, { topo, hash });
								}
								lumex::account_info new_info (hash, info->representative, info->open_block, block_a.hashables.balance, lumex::seconds_since_epoch (), info->block_count + 1, lumex::epoch::epoch_0);
								ledger.update_account (transaction, account, *info, new_info);
								ledger.store.pending.put (transaction, lumex::pending_key (block_a.hashables.destination, hash), { account, amount, lumex::epoch::epoch_0 });
								ledger.stats.inc (lumex::stat::type::ledger, lumex::stat::detail::send);
							}
						}
					}
				}
			}
		}
	}
}

void lumex::ledger_processor::receive_block (lumex::receive_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing = ledger.any.block_exists_or_pruned (transaction, hash);
	result = existing ? lumex::block_status::old : lumex::block_status::progress; // Have we seen this block already?  (Harmless)
	if (result == lumex::block_status::progress)
	{
		auto previous (ledger.store.block.get (transaction, block_a.hashables.previous));
		result = previous != nullptr ? lumex::block_status::progress : lumex::block_status::gap_previous;
		if (result == lumex::block_status::progress)
		{
			result = block_a.valid_predecessor (*previous) ? lumex::block_status::progress : lumex::block_status::block_position;
			if (result == lumex::block_status::progress)
			{
				auto account = previous->account ();
				auto info = ledger.any.account_get (transaction, account);
				release_assert (info);
				result = info->head != block_a.hashables.previous ? lumex::block_status::fork : lumex::block_status::progress; // If we have the block but it's not the latest we have a signed fork (Malicious)
				if (result == lumex::block_status::progress)
				{
					result = validate_message (account, hash, block_a.signature) ? lumex::block_status::bad_signature : lumex::block_status::progress; // Is the signature valid (Malformed)
					if (result == lumex::block_status::progress)
					{
						debug_assert (!validate_message (account, hash, block_a.signature));
						result = ledger.any.block_exists_or_pruned (transaction, block_a.hashables.source) ? lumex::block_status::progress : lumex::block_status::gap_source; // Have we seen the source block already? (Harmless)
						if (result == lumex::block_status::progress)
						{
							result = info->head == block_a.hashables.previous ? lumex::block_status::progress : lumex::block_status::gap_previous; // Block doesn't immediately follow latest block (Harmless)
							if (result == lumex::block_status::progress)
							{
								lumex::pending_key key (account, block_a.hashables.source);
								auto pending = ledger.store.pending.get (transaction, key);
								result = !pending ? lumex::block_status::unreceivable : lumex::block_status::progress; // Has this source already been received (Malformed)
								if (result == lumex::block_status::progress)
								{
									result = pending.value ().epoch == lumex::epoch::epoch_0 ? lumex::block_status::progress : lumex::block_status::unreceivable; // Are we receiving a state-only send? (Malformed)
									if (result == lumex::block_status::progress)
									{
										lumex::block_details block_details (lumex::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */);
										result = ledger.work.difficulty (block_a) >= ledger.work.threshold (block_a.work_version (), block_details) ? lumex::block_status::progress : lumex::block_status::insufficient_work; // Does this block have sufficient work? (Malformed)
										if (result == lumex::block_status::progress)
										{
											auto new_balance (info->balance.number () + pending.value ().amount.number ());
											ledger.store.pending.del (transaction, key);
											std::shared_ptr<lumex::block> source;
											if (ledger.flags.topo_index)
											{
												source = ledger.store.block.get (transaction, block_a.hashables.source);
												release_assert (source); // topo_index is incompatible with pruning; deps must be present
											}
											auto const topo = topology_height (previous, source);
											block_a.sideband_set (lumex::block_sideband{
											/* account */ account,
											/* balance */ new_balance,
											/* height */ info->block_count + 1,
											/* timestamp */ lumex::seconds_since_epoch (),
											/* details */ block_details,
											/* source_epoch */ lumex::epoch::epoch_0,
											/* topo_height */ topo });
											ledger.store.block.put (transaction, hash, block_a);
											if (topo != 0)
											{
												ledger.store.topology.put (transaction, { topo, hash });
											}
											lumex::account_info new_info (hash, info->representative, info->open_block, new_balance, lumex::seconds_since_epoch (), info->block_count + 1, lumex::epoch::epoch_0);
											ledger.update_account (transaction, account, *info, new_info);
											ledger.rep_weights.add (transaction, info->representative, pending.value ().amount);
											ledger.stats.inc (lumex::stat::type::ledger, lumex::stat::detail::receive);
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
}

void lumex::ledger_processor::open_block (lumex::open_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing = ledger.any.block_exists_or_pruned (transaction, hash);
	result = existing ? lumex::block_status::old : lumex::block_status::progress; // Have we seen this block already? (Harmless)
	if (result == lumex::block_status::progress)
	{
		result = validate_message (block_a.hashables.account, hash, block_a.signature) ? lumex::block_status::bad_signature : lumex::block_status::progress; // Is the signature valid (Malformed)
		if (result == lumex::block_status::progress)
		{
			debug_assert (!validate_message (block_a.hashables.account, hash, block_a.signature));
			result = ledger.any.block_exists_or_pruned (transaction, block_a.hashables.source) ? lumex::block_status::progress : lumex::block_status::gap_source; // Have we seen the source block? (Harmless)
			if (result == lumex::block_status::progress)
			{
				lumex::account_info info;
				result = ledger.store.account.get (transaction, block_a.hashables.account, info) ? lumex::block_status::progress : lumex::block_status::fork; // Has this account already been opened? (Malicious)
				if (result == lumex::block_status::progress)
				{
					lumex::pending_key key (block_a.hashables.account, block_a.hashables.source);
					auto pending = ledger.store.pending.get (transaction, key);
					result = !pending ? lumex::block_status::unreceivable : lumex::block_status::progress; // Has this source already been received (Malformed)
					if (result == lumex::block_status::progress)
					{
						result = block_a.hashables.account == ledger.constants.burn_account ? lumex::block_status::opened_burn_account : lumex::block_status::progress; // Is it burning 0 account? (Malicious)
						if (result == lumex::block_status::progress)
						{
							result = pending.value ().epoch == lumex::epoch::epoch_0 ? lumex::block_status::progress : lumex::block_status::unreceivable; // Are we receiving a state-only send? (Malformed)
							if (result == lumex::block_status::progress)
							{
								lumex::block_details block_details (lumex::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */);
								result = ledger.work.difficulty (block_a) >= ledger.work.threshold (block_a.work_version (), block_details) ? lumex::block_status::progress : lumex::block_status::insufficient_work; // Does this block have sufficient work? (Malformed)
								if (result == lumex::block_status::progress)
								{
									ledger.store.pending.del (transaction, key);
									std::shared_ptr<lumex::block> source;
									if (ledger.flags.topo_index)
									{
										source = ledger.store.block.get (transaction, block_a.hashables.source);
										release_assert (source); // topo_index is incompatible with pruning; deps must be present
									}
									auto const topo = topology_height (source);
									block_a.sideband_set (lumex::block_sideband{
									/* account */ block_a.hashables.account,
									/* balance */ pending.value ().amount,
									/* height */ uint64_t{ 1 },
									/* timestamp */ lumex::seconds_since_epoch (),
									/* details */ block_details,
									/* source_epoch */ lumex::epoch::epoch_0,
									/* topo_height */ topo });
									ledger.store.block.put (transaction, hash, block_a);
									if (topo != 0)
									{
										ledger.store.topology.put (transaction, { topo, hash });
									}
									lumex::account_info new_info (hash, block_a.representative_field ().value (), hash, pending.value ().amount.number (), lumex::seconds_since_epoch (), 1, lumex::epoch::epoch_0);
									ledger.update_account (transaction, block_a.hashables.account, info, new_info);
									ledger.rep_weights.add (transaction, block_a.representative_field ().value (), pending.value ().amount);
									ledger.stats.inc (lumex::stat::type::ledger, lumex::stat::detail::open);
								}
							}
						}
					}
				}
			}
		}
	}
}

void lumex::ledger_processor::change_block (lumex::change_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing = ledger.any.block_exists_or_pruned (transaction, hash);
	result = existing ? lumex::block_status::old : lumex::block_status::progress; // Have we seen this block before? (Harmless)
	if (result == lumex::block_status::progress)
	{
		auto previous (ledger.store.block.get (transaction, block_a.hashables.previous));
		result = previous != nullptr ? lumex::block_status::progress : lumex::block_status::gap_previous; // Have we seen the previous block already? (Harmless)
		if (result == lumex::block_status::progress)
		{
			result = block_a.valid_predecessor (*previous) ? lumex::block_status::progress : lumex::block_status::block_position;
			if (result == lumex::block_status::progress)
			{
				auto account = previous->account ();
				auto info = ledger.any.account_get (transaction, account);
				release_assert (info);
				result = info->head != block_a.hashables.previous ? lumex::block_status::fork : lumex::block_status::progress;
				if (result == lumex::block_status::progress)
				{
					release_assert (info->head == block_a.hashables.previous);
					result = validate_message (account, hash, block_a.signature) ? lumex::block_status::bad_signature : lumex::block_status::progress; // Is this block signed correctly (Malformed)
					if (result == lumex::block_status::progress)
					{
						lumex::block_details block_details (lumex::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */);
						result = ledger.work.difficulty (block_a) >= ledger.work.threshold (block_a.work_version (), block_details) ? lumex::block_status::progress : lumex::block_status::insufficient_work; // Does this block have sufficient work? (Malformed)
						if (result == lumex::block_status::progress)
						{
							debug_assert (!validate_message (account, hash, block_a.signature));
							auto const topo = topology_height (previous);
							block_a.sideband_set (lumex::block_sideband{
							/* account */ account,
							/* balance */ info->balance,
							/* height */ info->block_count + 1,
							/* timestamp */ lumex::seconds_since_epoch (),
							/* details */ block_details,
							/* source_epoch */ lumex::epoch::epoch_0,
							/* topo_height */ topo });
							ledger.store.block.put (transaction, hash, block_a);
							if (topo != 0)
							{
								ledger.store.topology.put (transaction, { topo, hash });
							}
							auto balance = previous->balance ();
							ledger.rep_weights.move (transaction, info->representative, block_a.hashables.representative, balance);
							lumex::account_info new_info (hash, block_a.hashables.representative, info->open_block, info->balance, lumex::seconds_since_epoch (), info->block_count + 1, lumex::epoch::epoch_0);
							ledger.update_account (transaction, account, *info, new_info);
							ledger.stats.inc (lumex::stat::type::ledger, lumex::stat::detail::change);
						}
					}
				}
			}
		}
	}
}

void lumex::ledger_processor::state_block (lumex::state_block & block_a)
{
	result = lumex::block_status::progress;
	auto is_epoch_block = false;
	if (ledger.is_epoch_link (block_a.hashables.link))
	{
		// This function also modifies the result variable if epoch is mal-formed
		is_epoch_block = validate_epoch_block (block_a);
	}

	if (result == lumex::block_status::progress)
	{
		if (is_epoch_block)
		{
			epoch_block_impl (block_a);
		}
		else
		{
			state_block_impl (block_a);
		}
	}
}

void lumex::ledger_processor::state_block_impl (lumex::state_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing = ledger.any.block_exists_or_pruned (transaction, hash);
	result = existing ? lumex::block_status::old : lumex::block_status::progress; // Have we seen this block before? (Unambiguous)
	if (result == lumex::block_status::progress)
	{
		result = validate_message (block_a.hashables.account, hash, block_a.signature) ? lumex::block_status::bad_signature : lumex::block_status::progress; // Is this block signed correctly (Unambiguous)
		if (result == lumex::block_status::progress)
		{
			debug_assert (!validate_message (block_a.hashables.account, hash, block_a.signature));
			result = block_a.hashables.account.is_zero () ? lumex::block_status::opened_burn_account : lumex::block_status::progress; // Is this for the burn account? (Unambiguous)
			if (result == lumex::block_status::progress)
			{
				lumex::epoch epoch (lumex::epoch::epoch_0);
				lumex::epoch source_epoch (lumex::epoch::epoch_0);
				lumex::account_info info;
				lumex::amount amount (block_a.hashables.balance);
				auto is_send (false);
				auto is_receive (false);
				auto account_error (ledger.store.account.get (transaction, block_a.hashables.account, info));
				if (!account_error)
				{
					// Account already exists
					epoch = info.epoch ();
					result = block_a.hashables.previous.is_zero () ? lumex::block_status::fork : lumex::block_status::progress; // Has this account already been opened? (Ambigious)
					if (result == lumex::block_status::progress)
					{
						result = ledger.store.block.exists (transaction, block_a.hashables.previous) ? lumex::block_status::progress : lumex::block_status::gap_previous; // Does the previous block exist in the ledger? (Unambigious)
						if (result == lumex::block_status::progress)
						{
							is_send = block_a.hashables.balance < info.balance;
							is_receive = !is_send && !block_a.hashables.link.is_zero ();
							amount = is_send ? (info.balance.number () - amount.number ()) : (amount.number () - info.balance.number ());
							result = block_a.hashables.previous == info.head ? lumex::block_status::progress : lumex::block_status::fork; // Is the previous block the account's head block? (Ambigious)
						}
					}
				}
				else
				{
					// Account does not yet exists
					result = block_a.previous ().is_zero () ? lumex::block_status::progress : lumex::block_status::gap_previous; // Does the first block in an account yield 0 for previous() ? (Unambigious)
					if (result == lumex::block_status::progress)
					{
						is_receive = true;
						result = !block_a.hashables.link.is_zero () ? lumex::block_status::progress : lumex::block_status::gap_source; // Is the first block receiving from a send ? (Unambigious)
					}
				}
				if (result == lumex::block_status::progress)
				{
					if (!is_send)
					{
						if (!block_a.hashables.link.is_zero ())
						{
							result = ledger.any.block_exists_or_pruned (transaction, block_a.hashables.link.as_block_hash ()) ? lumex::block_status::progress : lumex::block_status::gap_source; // Have we seen the source block already? (Harmless)
							if (result == lumex::block_status::progress)
							{
								lumex::pending_key key (block_a.hashables.account, block_a.hashables.link.as_block_hash ());
								auto pending = ledger.store.pending.get (transaction, key);
								result = !pending ? lumex::block_status::unreceivable : lumex::block_status::progress; // Has this source already been received (Malformed)
								if (result == lumex::block_status::progress)
								{
									result = amount == pending.value ().amount ? lumex::block_status::progress : lumex::block_status::balance_mismatch;
									source_epoch = pending.value ().epoch;
									epoch = std::max (epoch, source_epoch);
								}
							}
						}
						else
						{
							// If there's no link, the balance must remain the same, only the representative can change
							result = amount.is_zero () ? lumex::block_status::progress : lumex::block_status::balance_mismatch;
						}
					}
				}
				if (result == lumex::block_status::progress)
				{
					lumex::block_details block_details (epoch, is_send, is_receive, false);
					result = ledger.work.difficulty (block_a) >= ledger.work.threshold (block_a.work_version (), block_details) ? lumex::block_status::progress : lumex::block_status::insufficient_work; // Does this block have sufficient work? (Malformed)
					if (result == lumex::block_status::progress)
					{
						ledger.stats.inc (lumex::stat::type::ledger, lumex::stat::detail::state_block);
						std::shared_ptr<lumex::block> prev_block;
						std::shared_ptr<lumex::block> source_block;
						if (ledger.flags.topo_index)
						{
							if (!block_a.hashables.previous.is_zero ())
							{
								prev_block = ledger.store.block.get (transaction, block_a.hashables.previous);
								release_assert (prev_block); // topo_index is incompatible with pruning; deps must be present
							}
							if (is_receive && !block_a.hashables.link.is_zero ())
							{
								source_block = ledger.store.block.get (transaction, block_a.hashables.link.as_block_hash ());
								release_assert (source_block); // topo_index is incompatible with pruning; deps must be present
							}
						}
						auto const topo = topology_height (prev_block, source_block);
						block_a.sideband_set (lumex::block_sideband{
						/* account */ block_a.hashables.account,
						/* balance */ block_a.hashables.balance,
						/* height */ info.block_count + 1,
						/* timestamp */ lumex::seconds_since_epoch (),
						/* details */ block_details,
						/* source_epoch */ source_epoch,
						/* topo_height */ topo });
						ledger.store.block.put (transaction, hash, block_a);
						if (topo != 0)
						{
							ledger.store.topology.put (transaction, { topo, hash });
						}

						if (!info.head.is_zero ())
						{
							// Move existing representation & add in amount delta
							// ledger.rep_weights.representation_add_dual (transaction, info.representative, 0 - info.balance.number (), block_a.hashables.representative, block_a.hashables.balance.number ());
							ledger.rep_weights.move_add_sub (transaction, info.representative, info.balance, block_a.hashables.representative, block_a.hashables.balance);
						}
						else
						{
							// Add in amount delta only
							ledger.rep_weights.add (transaction, block_a.hashables.representative, block_a.hashables.balance);
						}

						if (is_send)
						{
							lumex::pending_key key (block_a.hashables.link.as_account (), hash);
							lumex::pending_info info (block_a.hashables.account, amount.number (), epoch);
							ledger.store.pending.put (transaction, key, info);
						}
						else if (!block_a.hashables.link.is_zero ())
						{
							ledger.store.pending.del (transaction, lumex::pending_key (block_a.hashables.account, block_a.hashables.link.as_block_hash ()));
						}

						lumex::account_info new_info (hash, block_a.hashables.representative, info.open_block.is_zero () ? hash : info.open_block, block_a.hashables.balance, lumex::seconds_since_epoch (), info.block_count + 1, epoch);
						ledger.update_account (transaction, block_a.hashables.account, info, new_info);
					}
				}
			}
		}
	}
}

void lumex::ledger_processor::epoch_block_impl (lumex::state_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing = ledger.any.block_exists_or_pruned (transaction, hash);
	result = existing ? lumex::block_status::old : lumex::block_status::progress; // Have we seen this block before? (Unambiguous)
	if (result == lumex::block_status::progress)
	{
		result = validate_message (ledger.epoch_signer (block_a.hashables.link), hash, block_a.signature) ? lumex::block_status::bad_signature : lumex::block_status::progress; // Is this block signed correctly (Unambiguous)
		if (result == lumex::block_status::progress)
		{
			debug_assert (!validate_message (ledger.epoch_signer (block_a.hashables.link), hash, block_a.signature));
			result = block_a.hashables.account.is_zero () ? lumex::block_status::opened_burn_account : lumex::block_status::progress; // Is this for the burn account? (Unambiguous)
			if (result == lumex::block_status::progress)
			{
				lumex::account_info info;
				auto account_error (ledger.store.account.get (transaction, block_a.hashables.account, info));
				if (!account_error)
				{
					// Account already exists
					result = block_a.hashables.previous.is_zero () ? lumex::block_status::fork : lumex::block_status::progress; // Has this account already been opened? (Ambigious)
					if (result == lumex::block_status::progress)
					{
						result = block_a.hashables.previous == info.head ? lumex::block_status::progress : lumex::block_status::fork; // Is the previous block the account's head block? (Ambigious)
						if (result == lumex::block_status::progress)
						{
							result = block_a.hashables.representative == info.representative ? lumex::block_status::progress : lumex::block_status::representative_mismatch;
						}
					}
				}
				else
				{
					result = block_a.hashables.representative.is_zero () ? lumex::block_status::progress : lumex::block_status::representative_mismatch;
					// Non-exisitng account should have pending entries
					if (result == lumex::block_status::progress)
					{
						bool pending_exists = ledger.any.receivable_exists (transaction, block_a.hashables.account);
						result = pending_exists ? lumex::block_status::progress : lumex::block_status::gap_epoch_open_pending;
					}
				}
				if (result == lumex::block_status::progress)
				{
					auto epoch = ledger.constants.epochs.epoch (block_a.hashables.link);
					// Must be an epoch for an unopened account or the epoch upgrade must be sequential
					auto is_valid_epoch_upgrade = account_error ? static_cast<std::underlying_type_t<lumex::epoch>> (epoch) > 0 : lumex::epochs::is_sequential (info.epoch (), epoch);
					result = is_valid_epoch_upgrade ? lumex::block_status::progress : lumex::block_status::block_position;
					if (result == lumex::block_status::progress)
					{
						result = block_a.hashables.balance == info.balance ? lumex::block_status::progress : lumex::block_status::balance_mismatch;
						if (result == lumex::block_status::progress)
						{
							lumex::block_details block_details (epoch, false, false, true);
							result = ledger.work.difficulty (block_a) >= ledger.work.threshold (block_a.work_version (), block_details) ? lumex::block_status::progress : lumex::block_status::insufficient_work; // Does this block have sufficient work? (Malformed)
							if (result == lumex::block_status::progress)
							{
								ledger.stats.inc (lumex::stat::type::ledger, lumex::stat::detail::epoch_block);

								// Epoch open on an unopened account is the only block type with no
								// usable block-graph dep (no `previous`, and `link` is the epoch
								// payload rather than a block hash). It's still anchored to the graph
								// implicitly via the pending entry it requires, so we treat it as a
								// known rootless start and index it at 1. For an epoch *upgrade* on
								// an existing chain we go through the standard path keyed off `previous`.
								uint64_t topo = 0;
								if (ledger.flags.topo_index)
								{
									if (block_a.hashables.previous.is_zero ())
									{
										topo = 1;
									}
									else
									{
										auto prev_block = ledger.store.block.get (transaction, block_a.hashables.previous);
										release_assert (prev_block); // previous == info.head, which is never pruned
										topo = topology_height (prev_block);
									}
								}

								block_a.sideband_set (lumex::block_sideband{
								/* account */ block_a.hashables.account,
								/* balance */ block_a.hashables.balance,
								/* height */ info.block_count + 1,
								/* timestamp */ lumex::seconds_since_epoch (),
								/* details */ block_details,
								/* source_epoch */ lumex::epoch::epoch_0,
								/* topo_height */ topo });
								ledger.store.block.put (transaction, hash, block_a);
								if (topo != 0)
								{
									ledger.store.topology.put (transaction, { topo, hash });
								}
								lumex::account_info new_info (hash, block_a.hashables.representative, info.open_block.is_zero () ? hash : info.open_block, info.balance, lumex::seconds_since_epoch (), info.block_count + 1, epoch);
								ledger.update_account (transaction, block_a.hashables.account, info, new_info);
							}
						}
					}
				}
			}
		}
	}
}

bool lumex::ledger_processor::validate_epoch_block (lumex::state_block const & block_a)
{
	debug_assert (ledger.is_epoch_link (block_a.hashables.link));
	lumex::amount prev_balance (0);
	if (!block_a.hashables.previous.is_zero ())
	{
		result = ledger.store.block.exists (transaction, block_a.hashables.previous) ? lumex::block_status::progress : lumex::block_status::gap_previous;
		if (result == lumex::block_status::progress)
		{
			prev_balance = ledger.any.block_balance (transaction, block_a.hashables.previous).value ();
		}
		else
		{
			// Check for possible regular state blocks with epoch link (send subtype)
			if (validate_message (block_a.hashables.account, block_a.hash (), block_a.signature))
			{
				// Is epoch block signed correctly
				if (validate_message (ledger.epoch_signer (block_a.link_field ().value ()), block_a.hash (), block_a.signature))
				{
					result = lumex::block_status::bad_signature;
				}
			}
		}
	}
	return (block_a.hashables.balance == prev_balance);
}

uint64_t lumex::ledger_processor::topology_height (std::shared_ptr<lumex::block> const & dep1, std::shared_ptr<lumex::block> const & dep2) const
{
	if (!ledger.flags.topo_index)
	{
		return 0;
	}
	uint64_t result{ 0 };
	auto consider = [&result] (std::shared_ptr<lumex::block> const & dep) -> bool {
		if (!dep)
		{
			return true; // No dep at this slot — skip
		}
		auto const topo = dep->sideband ().topo_height;
		if (topo == 0)
		{
			return false; // Dependency unindexed — propagate the sentinel
		}
		result = std::max (result, topo + 1);
		return true;
	};
	if (!consider (dep1) || !consider (dep2))
	{
		return 0;
	}
	return result;
}