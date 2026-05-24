#include <lumex/lib/blocks.hpp>
#include <lumex/lib/jsonconfig.hpp>
#include <lumex/lib/vote.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/backlog_scan.hpp>
#include <lumex/node/election.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/scheduler/hinted.hpp>
#include <lumex/node/scheduler/optimistic.hpp>
#include <lumex/node/scheduler/priority.hpp>
#include <lumex/node/transport/fake.hpp>
#include <lumex/node/transport/inproc.hpp>
#include <lumex/node/vote_cache.hpp>
#include <lumex/node/vote_processor.hpp>
#include <lumex/node/vote_router.hpp>
#include <lumex/node/wallet.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/test_common/chains.hpp>
#include <lumex/test_common/system.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (vote_processor, codes)
{
	lumex::test::system system;
	auto node_config = system.default_config ();
	// Disable all election schedulers
	node_config.backlog_scan->enable = false;
	node_config.hinted_scheduler->enable = false;
	node_config.optimistic_scheduler->enable = false;
	auto & node = *system.add_node (node_config);

	auto blocks = lumex::test::setup_chain (system, node, 1, lumex::dev::genesis_key, false);
	auto vote = lumex::test::make_vote (lumex::dev::genesis_key, { blocks[0] }, lumex::vote::timestamp_min * 1, 0);
	auto vote_invalid = std::make_shared<lumex::vote> (*vote);
	vote_invalid->signature.bytes[0] ^= 1;
	auto channel (std::make_shared<lumex::transport::inproc::channel> (node, node));

	// Invalid signature
	ASSERT_EQ (lumex::vote_code::invalid, node.vote_processor.vote_blocking (vote_invalid, channel));

	// No ongoing election (vote goes to vote cache)
	ASSERT_EQ (lumex::vote_code::indeterminate, node.vote_processor.vote_blocking (vote, channel));

	// Clear vote cache before starting election
	node.vote_cache.clear ();

	// First vote from an account for an ongoing election
	node.start_election (blocks[0]);
	std::shared_ptr<lumex::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (blocks[0]->qualified_root ()));
	ASSERT_EQ (lumex::vote_code::vote, node.vote_processor.vote_blocking (vote, channel));

	// Processing the same vote is a replay
	ASSERT_EQ (lumex::vote_code::replay, node.vote_processor.vote_blocking (vote, channel));

	// Invalid takes precedence
	ASSERT_EQ (lumex::vote_code::invalid, node.vote_processor.vote_blocking (vote_invalid, channel));

	// Once the election is removed (confirmed / dropped) the vote is again indeterminate
	ASSERT_TRUE (node.active.erase (blocks[0]->qualified_root ()));
	ASSERT_EQ (lumex::vote_code::indeterminate, node.vote_processor.vote_blocking (vote, channel));
}

TEST (vote_processor, invalid_signature)
{
	lumex::test::system system{ 1 };
	auto & node = *system.nodes[0];
	auto chain = lumex::test::setup_chain (system, node, 1, lumex::dev::genesis_key, false);
	lumex::keypair key;
	auto vote = std::make_shared<lumex::vote> (key.pub, key.prv, lumex::vote::timestamp_min * 1, 0, std::vector<lumex::block_hash>{ chain[0]->hash () });
	auto vote_invalid = std::make_shared<lumex::vote> (*vote);
	vote_invalid->signature.bytes[0] ^= 1;
	auto channel = std::make_shared<lumex::transport::inproc::channel> (node, node);

	auto election = lumex::test::start_election (system, node, chain[0]->hash ());
	ASSERT_NE (election, nullptr);
	ASSERT_EQ (1, election->votes ().size ());

	node.vote_processor.vote (vote_invalid, channel);
	ASSERT_TIMELY_EQ (5s, 1, election->votes ().size ());
	node.vote_processor.vote (vote, channel);
	ASSERT_TIMELY_EQ (5s, 2, election->votes ().size ());
}

TEST (vote_processor, overflow)
{
	lumex::test::system system;
	lumex::node_flags node_flags;
	node_flags.vote_processor_capacity = 1;
	auto & node (*system.add_node (node_flags));
	lumex::keypair key;
	auto vote = lumex::test::make_vote (key, { lumex::dev::genesis }, lumex::vote::timestamp_min * 1, 0);
	auto channel (std::make_shared<lumex::transport::inproc::channel> (node, node));
	auto start_time = std::chrono::system_clock::now ();

	// No way to lock the processor, but queueing votes in quick succession must result in overflow
	size_t not_processed{ 0 };
	size_t const total{ 1000 };
	for (unsigned i = 0; i < total; ++i)
	{
		if (!node.vote_processor.vote (vote, channel))
		{
			++not_processed;
		}
	}
	ASSERT_GT (not_processed, 0);
	ASSERT_LT (not_processed, total);
	ASSERT_EQ (not_processed, node.stats.count (lumex::stat::type::vote_processor, lumex::stat::detail::overfill));

	// check that it did not timeout
	ASSERT_LT (std::chrono::system_clock::now () - start_time, 10s);
}

TEST (vote_processor, weights)
{
	lumex::test::system system (4);
	auto & node (*system.nodes[0]);

	// Create representatives of different weight levels
	auto const stake = lumex::dev::genesis->balance ().number ();
	auto const level0 = stake / 5000; // 0.02%
	auto const level1 = stake / 500; // 0.2%
	auto const level2 = stake / 50; // 2%

	lumex::keypair key0;
	lumex::keypair key1;
	lumex::keypair key2;

	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	system.wallet (1)->insert_adhoc (key0.prv);
	system.wallet (2)->insert_adhoc (key1.prv);
	system.wallet (3)->insert_adhoc (key2.prv);
	system.wallet (1)->set_representative (key0.pub);
	system.wallet (2)->set_representative (key1.pub);
	system.wallet (3)->set_representative (key2.pub);
	system.wallet (0)->send_sync (lumex::dev::genesis_key.pub, key0.pub, level0);
	system.wallet (0)->send_sync (lumex::dev::genesis_key.pub, key1.pub, level1);
	system.wallet (0)->send_sync (lumex::dev::genesis_key.pub, key2.pub, level2);

	// Wait for representatives
	ASSERT_TIMELY_EQ (10s, node.ledger.rep_weights.get_rep_amounts ().size (), 4);

	// Wait for rep tiers to be updated
	node.stats.clear ();
	ASSERT_TIMELY (5s, node.stats.count (lumex::stat::type::rep_tiers, lumex::stat::detail::updated) >= 2);

	ASSERT_TIMELY_EQ (5s, node.rep_tiers.tier (key0.pub), lumex::rep_tier::none);
	ASSERT_TIMELY_EQ (5s, node.rep_tiers.tier (key1.pub), lumex::rep_tier::tier_1);
	ASSERT_TIMELY_EQ (5s, node.rep_tiers.tier (key2.pub), lumex::rep_tier::tier_2);
	ASSERT_TIMELY_EQ (5s, node.rep_tiers.tier (lumex::dev::genesis_key.pub), lumex::rep_tier::tier_3);
}

// Verify that multiple votes from different accounts are processed and inserted into a single election
TEST (vote_processor, election)
{
	lumex::test::system system;
	auto node_config = system.default_config ();
	node_config.backlog_scan->enable = false;
	node_config.priority_scheduler->enable = false;
	node_config.hinted_scheduler->enable = false;
	node_config.optimistic_scheduler->enable = false;
	auto & node = *system.add_node (node_config);

	auto blocks = lumex::test::setup_chain (system, node, 1, lumex::dev::genesis_key, false);
	auto & block = blocks[0];

	auto election = lumex::test::start_election (system, node, block->hash ());
	ASSERT_NE (nullptr, election);
	ASSERT_EQ (1, election->votes ().size ()); // Just the initial vote

	// Create votes from multiple accounts
	lumex::keypair key1, key2, key3;
	auto vote1 = lumex::test::make_vote (key1, { block }, lumex::vote::timestamp_min * 1, 0);
	auto vote2 = lumex::test::make_vote (key2, { block }, lumex::vote::timestamp_min * 2, 0);
	auto vote3 = lumex::test::make_vote (key3, { block }, lumex::vote::timestamp_min * 3, 0);

	auto channel = lumex::test::fake_channel (node);

	// Process all votes
	node.vote_processor.vote (vote1, channel);
	node.vote_processor.vote (vote2, channel);
	node.vote_processor.vote (vote3, channel);

	// Verify all votes are inserted into the election
	ASSERT_TIMELY_EQ (5s, election->votes ().size (), 4); // Initial + 3 votes

	auto votes = election->votes ();
	ASSERT_TRUE (votes.contains (key1.pub));
	ASSERT_TRUE (votes.contains (key2.pub));
	ASSERT_TRUE (votes.contains (key3.pub));
	ASSERT_EQ (votes.at (key1.pub).timestamp, lumex::vote::timestamp_min * 1);
	ASSERT_EQ (votes.at (key2.pub).timestamp, lumex::vote::timestamp_min * 2);
	ASSERT_EQ (votes.at (key3.pub).timestamp, lumex::vote::timestamp_min * 3);
}

// Verify that a vote with multiple hashes is routed to multiple elections
TEST (vote_processor, multiple_elections)
{
	lumex::test::system system;
	auto node_config = system.default_config ();
	node_config.backlog_scan->enable = false;
	node_config.priority_scheduler->enable = false;
	node_config.hinted_scheduler->enable = false;
	node_config.optimistic_scheduler->enable = false;
	auto & node = *system.add_node (node_config);

	auto blocks = lumex::test::setup_chain (system, node, 4, lumex::dev::genesis_key, false);
	auto & block1 = blocks[0];
	auto & block2 = blocks[1];
	auto & block3 = blocks[2];
	auto & block4 = blocks[3];

	// Start elections for all blocks
	auto election1 = lumex::test::start_election (system, node, block1->hash ());
	auto election2 = lumex::test::start_election (system, node, block2->hash ());
	auto election3 = lumex::test::start_election (system, node, block3->hash ());
	auto election4 = lumex::test::start_election (system, node, block4->hash ());
	ASSERT_NE (nullptr, election1);
	ASSERT_NE (nullptr, election2);
	ASSERT_NE (nullptr, election3);
	ASSERT_NE (nullptr, election4);

	// Create a single vote containing all block hashes
	lumex::keypair key;
	auto vote = lumex::test::make_vote (key, { block1, block2, block3, block4 }, lumex::vote::timestamp_min * 1, 0);
	ASSERT_EQ (vote->hashes.size (), 4);

	auto channel = lumex::test::fake_channel (node);
	node.vote_processor.vote (vote, channel);

	// Verify the vote appears in each election
	ASSERT_TIMELY_EQ (5s, election1->votes ().size (), 2); // Initial + our vote
	ASSERT_TIMELY_EQ (5s, election2->votes ().size (), 2);
	ASSERT_TIMELY_EQ (5s, election3->votes ().size (), 2);
	ASSERT_TIMELY_EQ (5s, election4->votes ().size (), 2);

	ASSERT_TRUE (election1->votes ().contains (key.pub));
	ASSERT_TRUE (election2->votes ().contains (key.pub));
	ASSERT_TRUE (election3->votes ().contains (key.pub));
	ASSERT_TRUE (election4->votes ().contains (key.pub));
}

// Ensure that node behaves well with votes larger than 12 hashes, which was maximum before V26
TEST (vote_processor, large_votes)
{
	lumex::test::system system (1);
	auto & node = *system.nodes[0];

	const int count = 32;
	auto blocks = lumex::test::setup_chain (system, node, count, lumex::dev::genesis_key, /* do not confirm */ false);

	ASSERT_TRUE (lumex::test::start_elections (system, node, blocks));
	ASSERT_TIMELY (5s, lumex::test::active (node, blocks));

	auto vote = lumex::test::make_final_vote (lumex::dev::genesis_key, blocks);
	ASSERT_EQ (vote->hashes.size (), count);

	node.vote_processor.vote (vote, lumex::test::fake_channel (node));

	ASSERT_TIMELY (5s, lumex::test::confirmed (node, blocks));
}

/*
 * vote tests
 */

// Basic test to check that the timestamp mask is applied correctly on vote timestamp and duration fields
TEST (vote, timestamp_and_duration_masking)
{
	lumex::test::system system;
	lumex::keypair key;
	auto hash = std::vector<lumex::block_hash>{ lumex::dev::genesis->hash () };
	auto vote = std::make_shared<lumex::vote> (key.pub, key.prv, 0x123f, 0xf, hash);
	ASSERT_EQ (vote->timestamp (), 0x1230);
	ASSERT_EQ (vote->duration ().count (), 524288);
	ASSERT_EQ (vote->duration_bits (), 0xf);
}

// Test that a vote can encode an empty hash set
TEST (vote, empty_hashes)
{
	lumex::keypair key;
	auto vote = std::make_shared<lumex::vote> (key.pub, key.prv, 0, 0, std::vector<lumex::block_hash>{} /* empty */);
}
