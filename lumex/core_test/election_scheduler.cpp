#include <lumex/lib/blockbuilders.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/backlog_scan.hpp>
#include <lumex/node/cementing_set.hpp>
#include <lumex/node/election.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/scheduler/component.hpp>
#include <lumex/node/scheduler/hinted.hpp>
#include <lumex/node/scheduler/optimistic.hpp>
#include <lumex/node/scheduler/priority.hpp>
#include <lumex/node/wallet.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/test_common/chains.hpp>
#include <lumex/test_common/system.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <chrono>

using namespace std::chrono_literals;

namespace
{
lumex::keypair & keyzero ()
{
	static lumex::keypair result;
	return result;
}
lumex::keypair & key0 ()
{
	static lumex::keypair result;
	return result;
}
lumex::keypair & key1 ()
{
	static lumex::keypair result;
	return result;
}
lumex::keypair & key2 ()
{
	static lumex::keypair result;
	return result;
}
lumex::keypair & key3 ()
{
	static lumex::keypair result;
	return result;
}
std::shared_ptr<lumex::state_block> & blockzero ()
{
	lumex::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (keyzero ().pub)
						 .previous (0)
						 .representative (keyzero ().pub)
						 .balance (0)
						 .link (0)
						 .sign (keyzero ().prv, keyzero ().pub)
						 .work (0)
						 .build ();
	return result;
}
std::shared_ptr<lumex::state_block> & block0 ()
{
	lumex::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key0 ().pub)
						 .previous (0)
						 .representative (key0 ().pub)
						 .balance (lumex::Klumex_ratio)
						 .link (0)
						 .sign (key0 ().prv, key0 ().pub)
						 .work (0)
						 .build ();
	return result;
}
std::shared_ptr<lumex::state_block> & block1 ()
{
	lumex::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key1 ().pub)
						 .previous (0)
						 .representative (key1 ().pub)
						 .balance (lumex::lumex_ratio)
						 .link (0)
						 .sign (key1 ().prv, key1 ().pub)
						 .work (0)
						 .build ();
	return result;
}
std::shared_ptr<lumex::state_block> & block2 ()
{
	lumex::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key2 ().pub)
						 .previous (0)
						 .representative (key2 ().pub)
						 .balance (lumex::Klumex_ratio)
						 .link (0)
						 .sign (key2 ().prv, key2 ().pub)
						 .work (0)
						 .build ();
	return result;
}
std::shared_ptr<lumex::state_block> & block3 ()
{
	lumex::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key3 ().pub)
						 .previous (0)
						 .representative (key3 ().pub)
						 .balance (lumex::lumex_ratio)
						 .link (0)
						 .sign (key3 ().prv, key3 ().pub)
						 .work (0)
						 .build ();
	return result;
}
}

TEST (election_scheduler, activate_one)
{
	lumex::test::system system;

	lumex::node_config config;
	config.backlog_scan->enable = false;
	auto & node = *system.add_node (config);

	lumex::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (lumex::dev::genesis->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				 .link (lumex::dev::genesis_key.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();
	node.ledger.process (node.ledger.tx_begin_write (), send1);
	node.scheduler.priority.activate (node.ledger.tx_begin_read (), lumex::dev::genesis_key.pub);
	ASSERT_TIMELY (5s, node.scheduler.priority.empty ());
	ASSERT_TIMELY (5s, node.active.election (send1->qualified_root ()));
}

/*
 * Tests that an optimistic election can be transitioned to a priority election.
 *
 * The test:
 * 1. Creates a chain of 2 blocks with an optimistic election for the second block
 * 2. Confirms the first block in the chain
 * 3. Attempts to start a priority election for the second block
 * 4. Verifies that the existing optimistic election is transitioned to priority
 * 5. Verifies a new vote is broadcast after the transition
 */
TEST (election_scheduler, transition_optimistic_to_priority)
{
	lumex::test::system system;
	lumex::node_config config = system.default_config ();
	config.optimistic_scheduler->gap_threshold = 1;
	config.enable_voting = true;
	config.hinted_scheduler->enable = false;
	config.network_params.network.vote_broadcast_interval = 15000ms;
	auto & node = *system.add_node (config);

	// Add representative
	const lumex::uint128_t rep_weight = lumex::Klumex_ratio * 100;
	lumex::keypair rep = lumex::test::setup_rep (system, node, rep_weight);
	system.wallet (0)->insert_adhoc (rep.prv);

	// Create a chain of blocks - and trigger an optimistic election for the last block
	const int howmany_blocks = 2;
	auto chains = lumex::test::setup_chains (system, node, /* single chain */ 1, howmany_blocks, lumex::dev::genesis_key, /* do not confirm */ false);
	auto & [account, blocks] = chains.front ();

	// Wait for optimistic election to start for last block
	auto const & block = blocks.back ();
	ASSERT_TIMELY (5s, node.vote_router.active (block->hash ()));
	auto election = node.active.election (block->qualified_root ());
	ASSERT_EQ (election->behavior (), lumex::election_behavior::optimistic);
	ASSERT_TIMELY_EQ (1s, 1, election->current_status ().status.vote_broadcast_count);

	// Confirm first block to allow upgrading second block's election
	lumex::test::confirm (node.ledger, blocks.at (howmany_blocks - 1));

	// Attempt to start priority election for second block
	node.active.insert (block, lumex::election_behavior::priority);

	// Verify priority transition
	ASSERT_EQ (election->behavior (), lumex::election_behavior::priority);
	ASSERT_EQ (1, node.stats.count (lumex::stat::type::active_elections, lumex::stat::detail::transition_priority));
	// Verify vote broadcast after transitioning
	ASSERT_TIMELY_EQ (1s, 2, election->current_status ().status.vote_broadcast_count);
	ASSERT_TRUE (node.active.active (*block));
}

/**
 * Tests that the election scheduler and the active transactions container (AEC)
 * work in sync with regards to the node configuration value "active_elections.size".
 *
 * The test sets up two forcefully cemented blocks -- a send on the genesis account and a receive on a second account.
 * It then creates two other blocks, each a successor to one of the previous two,
 * and processes them locally (without the node starting elections for them, but just saving them to disk).
 *
 * Elections for these latter two (B1 and B2) are started by the test code manually via `election_scheduler::activate`.
 * The test expects E1 to start right off and take its seat into the AEC.
 * E2 is expected not to start though (because the AEC is full), so B2 should be awaiting in the scheduler's queue.
 *
 * As soon as the test code manually confirms E1 (and thus evicts it out of the AEC),
 * it is expected that E2 begins and the scheduler's queue becomes empty again.
 */
TEST (election_scheduler, no_vacancy)
{
	lumex::test::system system;

	lumex::node_config config = system.default_config ();
	config.active_elections->size = 1;
	config.priority_scheduler->reserved_elections = 0;
	config.backlog_scan->enable = false;
	auto & node = *system.add_node (config);

	lumex::state_block_builder builder{};
	lumex::keypair key{};

	// Activating accounts depends on confirmed dependencies. First, prepare 2 accounts
	auto send = builder.make_block ()
				.account (lumex::dev::genesis_key.pub)
				.previous (lumex::dev::genesis->hash ())
				.representative (lumex::dev::genesis_key.pub)
				.link (key.pub)
				.balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*system.work.generate (lumex::dev::genesis->hash ()))
				.build ();
	ASSERT_EQ (lumex::block_status::progress, node.process (send));
	node.cementing_set.add (send->hash ());

	auto receive = builder.make_block ()
				   .account (key.pub)
				   .previous (0)
				   .representative (key.pub)
				   .link (send->hash ())
				   .balance (lumex::Klumex_ratio)
				   .sign (key.prv, key.pub)
				   .work (*system.work.generate (key.pub))
				   .build ();
	ASSERT_EQ (lumex::block_status::progress, node.process (receive));
	node.cementing_set.add (receive->hash ());

	ASSERT_TIMELY (5s, lumex::test::confirmed (node, { send, receive }));

	// Second, process two eligible transactions
	auto block1 = builder.make_block ()
				  .account (lumex::dev::genesis_key.pub)
				  .previous (send->hash ())
				  .representative (lumex::dev::genesis_key.pub)
				  .link (lumex::dev::genesis_key.pub)
				  .balance (lumex::dev::constants.genesis_amount - 2 * lumex::Klumex_ratio)
				  .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				  .work (*system.work.generate (send->hash ()))
				  .build ();
	ASSERT_EQ (lumex::block_status::progress, node.process (block1));

	// There is vacancy so it should be inserted
	node.scheduler.priority.activate (node.ledger.tx_begin_read (), lumex::dev::genesis_key.pub);
	std::shared_ptr<lumex::election> election{};
	ASSERT_TIMELY (5s, (election = node.active.election (block1->qualified_root ())) != nullptr);
	ASSERT_TIMELY_EQ (5s, node.scheduler.priority.size (), 0);

	auto block2 = builder.make_block ()
				  .account (key.pub)
				  .previous (receive->hash ())
				  .representative (key.pub)
				  .link (key.pub)
				  .balance (0)
				  .sign (key.prv, key.pub)
				  .work (*system.work.generate (receive->hash ()))
				  .build ();
	ASSERT_EQ (lumex::block_status::progress, node.process (block2));

	// There is no vacancy so it should stay queued
	node.scheduler.priority.activate (node.ledger.tx_begin_read (), key.pub);
	ASSERT_TIMELY_EQ (5s, node.scheduler.priority.size (), 1);
	ASSERT_ALWAYS_EQ (500ms, node.scheduler.priority.size (), 1);
	ASSERT_EQ (node.active.election (block2->qualified_root ()), nullptr);

	// Election confirmed, next in queue should begin
	election->force_confirm ();
	ASSERT_TIMELY (5s, node.active.election (block2->qualified_root ()) != nullptr);
	ASSERT_TRUE (node.scheduler.priority.empty ());
}