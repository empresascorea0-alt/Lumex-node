#include <lumex/lib/blocks.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/backlog_scan.hpp>
#include <lumex/node/election.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/scheduler/priority.hpp>
#include <lumex/node/vote_router.hpp>
#include <lumex/test_common/chains.hpp>
#include <lumex/test_common/system.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <chrono>

using namespace std::chrono_literals;

/*
 * Ensure account gets activated for a single unconfirmed account chain
 */
TEST (optimistic_scheduler, activate_one)
{
	lumex::test::system system;

	lumex::node_config config;
	config.priority_scheduler->enable = false; // Disable priority scheduler to avoid interference
	auto & node = *system.add_node (config);

	// Needs to be greater than optimistic scheduler `gap_threshold`
	const int howmany_blocks = 64;

	auto chains = lumex::test::setup_chains (system, node, /* single chain */ 1, howmany_blocks, lumex::dev::genesis_key, /* do not confirm */ false);
	auto & [account, blocks] = chains.front ();

	// Confirm block towards at the beginning the chain, so gap between confirmation and account frontier is larger than `gap_threshold`
	lumex::test::confirm (node.ledger, blocks.at (11));

	// Ensure unconfirmed account head block gets activated
	auto const & block = blocks.back ();
	std::shared_ptr<lumex::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (block->qualified_root ()));
	ASSERT_EQ (election->behavior (), lumex::election_behavior::optimistic);
}

/*
 * Ensure account gets activated for a single unconfirmed account chain with nothing yet confirmed
 */
TEST (optimistic_scheduler, activate_one_zero_conf)
{
	lumex::test::system system;

	lumex::node_config config;
	config.priority_scheduler->enable = false; // Disable priority scheduler to avoid interference
	auto & node = *system.add_node (config);

	// Can be smaller than optimistic scheduler `gap_threshold`
	// This is meant to activate short account chains (eg. binary tree spam leaf accounts)
	const int howmany_blocks = 6;

	auto chains = lumex::test::setup_chains (system, node, /* single chain */ 1, howmany_blocks, lumex::dev::genesis_key, /* do not confirm */ false);
	auto & [account, blocks] = chains.front ();

	// Ensure unconfirmed account head block gets activated
	auto const & block = blocks.back ();
	std::shared_ptr<lumex::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (block->qualified_root ()));
	ASSERT_EQ (election->behavior (), lumex::election_behavior::optimistic);
}

/*
 * Ensure account gets activated for a multiple unconfirmed account chains
 */
TEST (optimistic_scheduler, activate_many)
{
	lumex::test::system system;

	lumex::node_config config;
	config.priority_scheduler->enable = false; // Disable priority scheduler to avoid interference
	auto & node = *system.add_node (config);

	// Needs to be greater than optimistic scheduler `gap_threshold`
	const int howmany_blocks = 64;
	const int howmany_chains = 16;

	auto chains = lumex::test::setup_chains (system, node, howmany_chains, howmany_blocks, lumex::dev::genesis_key, /* do not confirm */ false);

	// Ensure all unconfirmed account head blocks get activated
	ASSERT_TIMELY (15s, std::all_of (chains.begin (), chains.end (), [&] (auto const & entry) {
		auto const & [account, blocks] = entry;
		auto const & block = blocks.back ();
		auto election = node.active.election (block->qualified_root ());
		return election && election->behavior () == lumex::election_behavior::optimistic;
	}));
}

/*
 * Ensure accounts with some blocks already confirmed and with less than `gap_threshold` blocks do not get activated
 */
TEST (optimistic_scheduler, under_gap_threshold)
{
	lumex::test::system system;

	lumex::node_config config = system.default_config ();
	config.backlog_scan->enable = false;
	auto & node = *system.add_node (config);

	// Must be smaller than optimistic scheduler `gap_threshold`
	const int howmany_blocks = 64;

	auto chains = lumex::test::setup_chains (system, node, /* single chain */ 1, howmany_blocks, lumex::dev::genesis_key, /* do not confirm */ false);
	auto & [account, blocks] = chains.front ();

	// Confirm block towards the end of the chain, so gap between confirmation and account frontier is less than `gap_threshold`
	lumex::test::confirm (node.ledger, blocks.at (55));

	// Manually trigger backlog scan
	node.backlog_scan.trigger ();

	// Ensure unconfirmed account head block gets activated
	auto const & block = blocks.back ();
	ASSERT_NEVER (3s, node.vote_router.active (block->hash ()));
}
