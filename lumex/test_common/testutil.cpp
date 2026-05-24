#include <lumex/crypto_lib/random_pool.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/vote.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/election.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/scheduler/component.hpp>
#include <lumex/node/scheduler/manual.hpp>
#include <lumex/node/scheduler/priority.hpp>
#include <lumex/node/transport/fake.hpp>
#include <lumex/node/vote_router.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_set_any.hpp>
#include <lumex/secure/ledger_set_cemented.hpp>
#include <lumex/store/ledger/account.hpp>
#include <lumex/store/ledger/block.hpp>
#include <lumex/store/ledger/confirmation_height.hpp>
#include <lumex/store/ledger/peer.hpp>
#include <lumex/store/ledger/pending.hpp>
#include <lumex/test_common/system.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <numeric>

using namespace std::chrono_literals;

void lumex::test::wait_peer_connections (lumex::test::system & system_a)
{
	auto wait_peer_count = [&system_a] (bool in_memory) {
		auto num_nodes = system_a.nodes.size ();
		system_a.deadline_set (20s);
		size_t peer_count = 0;
		while (peer_count != num_nodes * (num_nodes - 1))
		{
			ASSERT_NO_ERROR (system_a.poll ());
			peer_count = std::accumulate (system_a.nodes.cbegin (), system_a.nodes.cend (), std::size_t{ 0 }, [in_memory] (auto total, auto const & node) {
				if (in_memory)
				{
					return total += node->network.size ();
				}
				else
				{
					auto transaction = node->store.tx_begin_read ();
					return total += node->store.peer.count (transaction);
				}
			});
		}
	};

	// Do a pre-pass with in-memory containers to reduce IO if still in the process of connecting to peers
	wait_peer_count (true);
	wait_peer_count (false);
}

bool lumex::test::process (lumex::node & node, std::vector<std::shared_ptr<lumex::block>> blocks)
{
	auto const transaction = node.ledger.tx_begin_write ();
	for (auto & block : blocks)
	{
		auto result = node.process (transaction, block);
		debug_assert (result == lumex::block_status::progress || result == lumex::block_status::old);
	}
	return true;
}

bool lumex::test::process_live (lumex::node & node, std::vector<std::shared_ptr<lumex::block>> blocks)
{
	for (auto & block : blocks)
	{
		node.process_active (block);
	}
	return true;
}

bool lumex::test::confirmed (lumex::node & node, std::vector<lumex::block_hash> hashes)
{
	for (auto & hash : hashes)
	{
		if (!node.block_confirmed (hash))
		{
			return false;
		}
	}
	return true;
}

bool lumex::test::confirmed (lumex::node & node, std::vector<std::shared_ptr<lumex::block>> blocks)
{
	return confirmed (node, blocks_to_hashes (blocks));
}

bool lumex::test::exists (lumex::node & node, std::vector<lumex::block_hash> hashes)
{
	for (auto & hash : hashes)
	{
		if (!node.block (hash))
		{
			return false;
		}
	}
	return true;
}

bool lumex::test::exists (lumex::node & node, std::vector<std::shared_ptr<lumex::block>> blocks)
{
	return exists (node, blocks_to_hashes (blocks));
}

void lumex::test::confirm (lumex::node & node, std::vector<std::shared_ptr<lumex::block>> const blocks)
{
	confirm (node.ledger, blocks);
}

void lumex::test::confirm (lumex::ledger & ledger, std::vector<std::shared_ptr<lumex::block>> const blocks)
{
	for (auto const block : blocks)
	{
		confirm (ledger, block);
	}
}

void lumex::test::confirm (lumex::ledger & ledger, std::shared_ptr<lumex::block> const block)
{
	confirm (ledger, block->hash ());
}

void lumex::test::confirm (lumex::ledger & ledger, lumex::block_hash const & hash)
{
	auto transaction = ledger.tx_begin_write ();
	ledger.cement (transaction, hash);
}

bool lumex::test::block_or_pruned_all_exists (lumex::node & node, std::vector<lumex::block_hash> hashes)
{
	auto transaction = node.ledger.tx_begin_read ();
	return std::all_of (hashes.begin (), hashes.end (),
	[&] (const auto & hash) {
		return node.ledger.any.block_exists_or_pruned (transaction, hash);
	});
}

bool lumex::test::block_or_pruned_all_exists (lumex::node & node, std::vector<std::shared_ptr<lumex::block>> blocks)
{
	return block_or_pruned_all_exists (node, blocks_to_hashes (blocks));
}

bool lumex::test::block_or_pruned_none_exists (lumex::node & node, std::vector<lumex::block_hash> hashes)
{
	auto transaction = node.ledger.tx_begin_read ();
	return std::none_of (hashes.begin (), hashes.end (),
	[&] (const auto & hash) {
		return node.ledger.any.block_exists_or_pruned (transaction, hash);
	});
}

bool lumex::test::block_or_pruned_none_exists (lumex::node & node, std::vector<std::shared_ptr<lumex::block>> blocks)
{
	return block_or_pruned_none_exists (node, blocks_to_hashes (blocks));
}

bool lumex::test::activate (lumex::node & node, std::vector<lumex::block_hash> hashes)
{
	for (auto & hash : hashes)
	{
		auto disk_block = node.block (hash);
		if (disk_block == nullptr)
		{
			// Block does not exist in the ledger yet
			return false;
		}
		node.scheduler.manual.push (disk_block);
	}
	return true;
}

bool lumex::test::activate (lumex::node & node, std::vector<std::shared_ptr<lumex::block>> blocks)
{
	return activate (node, blocks_to_hashes (blocks));
}

bool lumex::test::active (lumex::node & node, std::vector<lumex::block_hash> hashes)
{
	for (auto & hash : hashes)
	{
		if (!node.vote_router.active (hash))
		{
			return false;
		}
	}
	return true;
}

bool lumex::test::active (lumex::node & node, std::vector<std::shared_ptr<lumex::block>> blocks)
{
	return active (node, blocks_to_hashes (blocks));
}

std::shared_ptr<lumex::vote> lumex::test::make_vote (lumex::keypair key, std::vector<lumex::block_hash> hashes, uint64_t timestamp, uint8_t duration)
{
	return std::make_shared<lumex::vote> (key.pub, key.prv, timestamp, duration, hashes);
}

std::shared_ptr<lumex::vote> lumex::test::make_vote (lumex::keypair key, std::vector<std::shared_ptr<lumex::block>> blocks, uint64_t timestamp, uint8_t duration)
{
	std::vector<lumex::block_hash> hashes;
	std::transform (blocks.begin (), blocks.end (), std::back_inserter (hashes), [] (auto & block) { return block->hash (); });
	return make_vote (key, hashes, timestamp, duration);
}

std::shared_ptr<lumex::vote> lumex::test::make_final_vote (lumex::keypair key, std::vector<lumex::block_hash> hashes)
{
	return make_vote (key, hashes, lumex::vote::timestamp_max, lumex::vote::duration_max);
}

std::shared_ptr<lumex::vote> lumex::test::make_final_vote (lumex::keypair key, std::vector<std::shared_ptr<lumex::block>> blocks)
{
	return make_vote (key, blocks, lumex::vote::timestamp_max, lumex::vote::duration_max);
}

std::vector<lumex::block_hash> lumex::test::blocks_to_hashes (std::vector<std::shared_ptr<lumex::block>> blocks)
{
	std::vector<lumex::block_hash> hashes;
	std::transform (blocks.begin (), blocks.end (), std::back_inserter (hashes), [] (auto & block) { return block->hash (); });
	return hashes;
}

std::vector<std::shared_ptr<lumex::block>> lumex::test::clone (std::vector<std::shared_ptr<lumex::block>> blocks)
{
	std::vector<std::shared_ptr<lumex::block>> clones;
	std::transform (blocks.begin (), blocks.end (), std::back_inserter (clones), [] (auto & block) { return block->clone (); });
	return clones;
}

std::shared_ptr<lumex::transport::channel> lumex::test::fake_channel (lumex::node & node, lumex::account node_id)
{
	auto channel = std::make_shared<lumex::transport::fake::channel> (node);
	if (!node_id.is_zero ())
	{
		channel->set_node_id (node_id);
	}
	return channel;
}

std::shared_ptr<lumex::transport::test_channel> lumex::test::test_channel (lumex::node & node, lumex::account node_id)
{
	auto channel = std::make_shared<lumex::transport::test_channel> (node);
	if (!node_id.is_zero ())
	{
		channel->set_node_id (node_id);
	}
	return channel;
}

std::shared_ptr<lumex::election> lumex::test::start_election (lumex::test::system & system, lumex::node & node, const lumex::block_hash & hash)
{
	system.deadline_set (5s);

	// Wait until and ensure that the block is in the ledger
	auto block_l = node.block (hash);
	debug_assert (block_l);

	auto fut = node.scheduler.manual.push (block_l);

	// Wait for the block to be scheduled
	auto status = fut.wait_for (5s);
	debug_assert (status == std::future_status::ready);

	auto election = fut.get ();
	return election;
}

bool lumex::test::start_elections (lumex::test::system & system, lumex::node & node, std::vector<lumex::block_hash> const & hashes, bool const forced)
{
	for (auto const & hash_l : hashes)
	{
		auto election = start_election (system, node, hash_l);
		if (!election)
		{
			return false;
		}
		if (forced)
		{
			election->force_confirm ();
		}
	}
	return true;
}

bool lumex::test::start_elections (lumex::test::system & system, lumex::node & node, std::vector<std::shared_ptr<lumex::block>> const & blocks, bool const forced)
{
	return start_elections (system, node, blocks_to_hashes (blocks), forced);
}

lumex::account_info lumex::test::account_info (lumex::node const & node, lumex::account const & acc)
{
	auto const tx = node.ledger.tx_begin_read ();
	auto opt = node.ledger.any.account_get (tx, acc);
	if (opt.has_value ())
	{
		return opt.value ();
	}
	return {};
}

void lumex::test::print_all_receivable_entries (const lumex::store::ledger_store & store)
{
	std::cout << "Printing all receivable entries:\n";
	auto const tx = store.tx_begin_read ();
	auto const end = store.pending.end (tx);
	for (auto i = store.pending.begin (tx); i != end; ++i)
	{
		std::cout << "Key:  " << i->first << std::endl;
		std::cout << "Info: " << i->second << std::endl;
	}
}

void lumex::test::print_all_account_info (const lumex::ledger & ledger)
{
	std::cout << "Printing all account info:\n";
	auto const tx = ledger.tx_begin_read ();
	auto const end = ledger.store.account.end (tx);
	for (auto i = ledger.store.account.begin (tx); i != end; ++i)
	{
		lumex::account acc = i->first;
		lumex::account_info acc_info = i->second;
		lumex::confirmation_height_info height_info;
		std::cout << "Account: " << acc.to_account () << std::endl;
		std::cout << "  Unconfirmed Balance: " << acc_info.balance.to_string_dec () << std::endl;
		std::cout << "  Confirmed Balance:   " << ledger.cemented.account_balance (tx, acc).value_or (0) << std::endl;
		std::cout << "  Block Count:         " << acc_info.block_count << std::endl;
		if (!ledger.store.confirmation_height.get (tx, acc, height_info))
		{
			std::cout << "  Conf. Height:        " << height_info.height << std::endl;
			std::cout << "  Conf. Frontier:      " << height_info.frontier.to_string () << std::endl;
		}
	}
}

void lumex::test::print_all_blocks (const lumex::store::ledger_store & store)
{
	auto tx = store.tx_begin_read ();
	auto i = store.block.begin (tx);
	auto end = store.block.end (tx);
	std::cout << "Listing all blocks" << std::endl;
	for (; i != end; ++i)
	{
		lumex::block_hash hash = i->first;
		lumex::store::block_w_sideband sideband = i->second;
		std::shared_ptr<lumex::block> b = sideband.block;
		std::cout << "Hash: " << hash.to_string () << std::endl;
		const auto acc = sideband.sideband.account;
		std::cout << "Acc: " << acc.to_string () << "(" << acc.to_account () << ")" << std::endl;
		std::cout << "Height: " << sideband.sideband.height << std::endl;
		std::cout << b->to_json ();
	}
}

std::vector<std::shared_ptr<lumex::block>> lumex::test::all_blocks (lumex::node & node)
{
	auto transaction = node.store.tx_begin_read ();
	std::vector<std::shared_ptr<lumex::block>> result;
	for (auto it = node.store.block.begin (transaction), end = node.store.block.end (transaction); it != end; ++it)
	{
		result.push_back (it->second.block);
	}
	return result;
}

lumex::uint128_t lumex::test::minimum_principal_weight ()
{
	return lumex::dev::genesis->balance ().number () / lumex::dev::network_params.network.principal_weight_factor;
}
