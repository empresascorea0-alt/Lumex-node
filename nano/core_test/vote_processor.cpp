#include <nano/lib/blocks.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/vote.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/election.hpp>
#include <nano/node/transport/fake.hpp>
#include <nano/node/transport/inproc.hpp>
#include <nano/node/vote_processor.hpp>
#include <nano/node/vote_router.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (vote_processor, codes)
{
	nano::test::system system;
	auto node_config = system.default_config ();
	// Disable all election schedulers
	node_config.backlog_scan.enable = false;
	node_config.hinted_scheduler.enable = false;
	node_config.optimistic_scheduler.enable = false;
	auto & node = *system.add_node (node_config);

	auto blocks = nano::test::setup_chain (system, node, 1, nano::dev::genesis_key, false);
	auto vote = nano::test::make_vote (nano::dev::genesis_key, { blocks[0] }, nano::vote::timestamp_min * 1, 0);
	auto vote_invalid = std::make_shared<nano::vote> (*vote);
	vote_invalid->signature.bytes[0] ^= 1;
	auto channel (std::make_shared<nano::transport::inproc::channel> (node, node));

	// Invalid signature
	ASSERT_EQ (nano::vote_code::invalid, node.vote_processor.vote_blocking (vote_invalid, channel));

	// No ongoing election (vote goes to vote cache)
	ASSERT_EQ (nano::vote_code::indeterminate, node.vote_processor.vote_blocking (vote, channel));

	// Clear vote cache before starting election
	node.vote_cache.clear ();

	// First vote from an account for an ongoing election
	node.start_election (blocks[0]);
	std::shared_ptr<nano::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (blocks[0]->qualified_root ()));
	ASSERT_EQ (nano::vote_code::vote, node.vote_processor.vote_blocking (vote, channel));

	// Processing the same vote is a replay
	ASSERT_EQ (nano::vote_code::replay, node.vote_processor.vote_blocking (vote, channel));

	// Invalid takes precedence
	ASSERT_EQ (nano::vote_code::invalid, node.vote_processor.vote_blocking (vote_invalid, channel));

	// Once the election is removed (confirmed / dropped) the vote is again indeterminate
	ASSERT_TRUE (node.active.erase (blocks[0]->qualified_root ()));
	ASSERT_EQ (nano::vote_code::indeterminate, node.vote_processor.vote_blocking (vote, channel));
}

TEST (vote_processor, invalid_signature)
{
	nano::test::system system{ 1 };
	auto & node = *system.nodes[0];
	auto chain = nano::test::setup_chain (system, node, 1, nano::dev::genesis_key, false);
	nano::keypair key;
	auto vote = std::make_shared<nano::vote> (key.pub, key.prv, nano::vote::timestamp_min * 1, 0, std::vector<nano::block_hash>{ chain[0]->hash () });
	auto vote_invalid = std::make_shared<nano::vote> (*vote);
	vote_invalid->signature.bytes[0] ^= 1;
	auto channel = std::make_shared<nano::transport::inproc::channel> (node, node);

	auto election = nano::test::start_election (system, node, chain[0]->hash ());
	ASSERT_NE (election, nullptr);
	ASSERT_EQ (1, election->votes ().size ());

	node.vote_processor.vote (vote_invalid, channel);
	ASSERT_TIMELY_EQ (5s, 1, election->votes ().size ());
	node.vote_processor.vote (vote, channel);
	ASSERT_TIMELY_EQ (5s, 2, election->votes ().size ());
}

TEST (vote_processor, overflow)
{
	nano::test::system system;
	nano::node_flags node_flags;
	node_flags.vote_processor_capacity = 1;
	auto & node (*system.add_node (node_flags));
	nano::keypair key;
	auto vote = nano::test::make_vote (key, { nano::dev::genesis }, nano::vote::timestamp_min * 1, 0);
	auto channel (std::make_shared<nano::transport::inproc::channel> (node, node));
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
	ASSERT_EQ (not_processed, node.stats.count (nano::stat::type::vote_processor, nano::stat::detail::overfill));

	// check that it did not timeout
	ASSERT_LT (std::chrono::system_clock::now () - start_time, 10s);
}

TEST (vote_processor, weights)
{
	nano::test::system system (4);
	auto & node (*system.nodes[0]);

	// Create representatives of different weight levels
	auto const stake = nano::dev::genesis->balance ().number ();
	auto const level0 = stake / 5000; // 0.02%
	auto const level1 = stake / 500; // 0.2%
	auto const level2 = stake / 50; // 2%

	nano::keypair key0;
	nano::keypair key1;
	nano::keypair key2;

	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	system.wallet (1)->insert_adhoc (key0.prv);
	system.wallet (2)->insert_adhoc (key1.prv);
	system.wallet (3)->insert_adhoc (key2.prv);
	system.wallet (1)->store.representative_set (system.nodes[1]->wallets.tx_begin_write (), key0.pub);
	system.wallet (2)->store.representative_set (system.nodes[2]->wallets.tx_begin_write (), key1.pub);
	system.wallet (3)->store.representative_set (system.nodes[3]->wallets.tx_begin_write (), key2.pub);
	system.wallet (0)->send_sync (nano::dev::genesis_key.pub, key0.pub, level0);
	system.wallet (0)->send_sync (nano::dev::genesis_key.pub, key1.pub, level1);
	system.wallet (0)->send_sync (nano::dev::genesis_key.pub, key2.pub, level2);

	// Wait for representatives
	ASSERT_TIMELY_EQ (10s, node.ledger.rep_weights.get_rep_amounts ().size (), 4);

	// Wait for rep tiers to be updated
	node.stats.clear ();
	ASSERT_TIMELY (5s, node.stats.count (nano::stat::type::rep_tiers, nano::stat::detail::updated) >= 2);

	ASSERT_TIMELY_EQ (5s, node.rep_tiers.tier (key0.pub), nano::rep_tier::none);
	ASSERT_TIMELY_EQ (5s, node.rep_tiers.tier (key1.pub), nano::rep_tier::tier_1);
	ASSERT_TIMELY_EQ (5s, node.rep_tiers.tier (key2.pub), nano::rep_tier::tier_2);
	ASSERT_TIMELY_EQ (5s, node.rep_tiers.tier (nano::dev::genesis_key.pub), nano::rep_tier::tier_3);
}

// Ensure that node behaves well with votes larger than 12 hashes, which was maximum before V26
TEST (vote_processor, large_votes)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];

	const int count = 32;
	auto blocks = nano::test::setup_chain (system, node, count, nano::dev::genesis_key, /* do not confirm */ false);

	ASSERT_TRUE (nano::test::start_elections (system, node, blocks));
	ASSERT_TIMELY (5s, nano::test::active (node, blocks));

	auto vote = nano::test::make_final_vote (nano::dev::genesis_key, blocks);
	ASSERT_EQ (vote->hashes.size (), count);

	node.vote_processor.vote (vote, nano::test::fake_channel (node));

	ASSERT_TIMELY (5s, nano::test::confirmed (node, blocks));
}

// Basic test to check that the timestamp mask is applied correctly on vote timestamp and duration fields
TEST (vote, timestamp_and_duration_masking)
{
	nano::test::system system;
	nano::keypair key;
	auto hash = std::vector<nano::block_hash>{ nano::dev::genesis->hash () };
	auto vote = std::make_shared<nano::vote> (key.pub, key.prv, 0x123f, 0xf, hash);
	ASSERT_EQ (vote->timestamp (), 0x1230);
	ASSERT_EQ (vote->duration ().count (), 524288);
	ASSERT_EQ (vote->duration_bits (), 0xf);
}

// Test that a vote can encode an empty hash set
TEST (vote, empty_hashes)
{
	nano::keypair key;
	auto vote = std::make_shared<nano::vote> (key.pub, key.prv, 0, 0, std::vector<nano::block_hash>{} /* empty */);
}
