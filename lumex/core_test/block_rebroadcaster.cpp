#include <lumex/lib/blockbuilders.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/block_rebroadcaster.hpp>
#include <lumex/node/bootstrap/bootstrap_config.hpp>
#include <lumex/node/bootstrap/bootstrap_service.hpp>
#include <lumex/node/local_block_broadcaster.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/vote_rebroadcaster.hpp>
#include <lumex/test_common/system.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

/*
 * block_rebroadcaster_index tests
 */

TEST (block_rebroadcaster_index, empty)
{
	lumex::block_rebroadcaster_config config;
	lumex::block_rebroadcaster_index index{ config };

	ASSERT_EQ (0, index.size ());
	ASSERT_FALSE (index.contains (lumex::block_hash{ 1 }));
}

TEST (block_rebroadcaster_index, insert_and_lookup)
{
	lumex::block_rebroadcaster_config config;
	lumex::block_rebroadcaster_index index{ config };

	lumex::block_hash hash{ 1 };
	auto now = std::chrono::steady_clock::now ();

	// First insert should succeed
	ASSERT_TRUE (index.check_and_record (hash, now));
	ASSERT_EQ (1, index.size ());
	ASSERT_TRUE (index.contains (hash));
}

TEST (block_rebroadcaster_index, cooldown)
{
	lumex::block_rebroadcaster_config config;
	config.rebroadcast_cooldown = 100ms;
	lumex::block_rebroadcaster_index index{ config };

	lumex::block_hash hash{ 1 };
	auto now = std::chrono::steady_clock::now ();

	// First insert should succeed
	ASSERT_TRUE (index.check_and_record (hash, now));

	// Within cooldown period should fail
	ASSERT_FALSE (index.check_and_record (hash, now + 99ms));
	ASSERT_FALSE (index.check_and_record (hash, now + 50ms));

	// At exactly cooldown should succeed
	ASSERT_TRUE (index.check_and_record (hash, now + 100ms));
}

TEST (block_rebroadcaster_index, timestamp_update)
{
	lumex::block_rebroadcaster_config config;
	config.rebroadcast_cooldown = 100ms;
	lumex::block_rebroadcaster_index index{ config };

	lumex::block_hash hash{ 1 };
	auto t1 = std::chrono::steady_clock::now ();

	// Insert at T1
	ASSERT_TRUE (index.check_and_record (hash, t1));

	// Re-record at T2 (after cooldown expires)
	auto t2 = t1 + 100ms;
	ASSERT_TRUE (index.check_and_record (hash, t2));

	// Immediately after T2 should fail (proves timestamp was updated to T2)
	ASSERT_FALSE (index.check_and_record (hash, t2 + 1ms));

	// After cooldown from T2 should succeed
	ASSERT_TRUE (index.check_and_record (hash, t2 + 100ms));
}

TEST (block_rebroadcaster_index, max_history)
{
	lumex::block_rebroadcaster_config config;
	config.max_history = 3;
	lumex::block_rebroadcaster_index index{ config };

	auto now = std::chrono::steady_clock::now ();

	// Insert 5 hashes
	for (uint64_t i = 1; i <= 5; ++i)
	{
		ASSERT_TRUE (index.check_and_record (lumex::block_hash{ i }, now));
	}

	// Size should be capped at max_history
	ASSERT_EQ (3, index.size ());

	// Oldest 2 should be evicted (FIFO)
	ASSERT_FALSE (index.contains (lumex::block_hash{ 1 }));
	ASSERT_FALSE (index.contains (lumex::block_hash{ 2 }));

	// Newest 3 should remain
	ASSERT_TRUE (index.contains (lumex::block_hash{ 3 }));
	ASSERT_TRUE (index.contains (lumex::block_hash{ 4 }));
	ASSERT_TRUE (index.contains (lumex::block_hash{ 5 }));
}

TEST (block_rebroadcaster_index, fifo_eviction)
{
	lumex::block_rebroadcaster_config config;
	config.max_history = 2;
	lumex::block_rebroadcaster_index index{ config };

	auto now = std::chrono::steady_clock::now ();

	lumex::block_hash hash1{ 1 };
	lumex::block_hash hash2{ 2 };
	lumex::block_hash hash3{ 3 };

	ASSERT_TRUE (index.check_and_record (hash1, now));
	ASSERT_TRUE (index.check_and_record (hash2, now));
	ASSERT_EQ (2, index.size ());

	// Adding third should evict first (oldest)
	ASSERT_TRUE (index.check_and_record (hash3, now));
	ASSERT_EQ (2, index.size ());

	ASSERT_FALSE (index.contains (hash1)); // Evicted
	ASSERT_TRUE (index.contains (hash2));
	ASSERT_TRUE (index.contains (hash3));
}

TEST (block_rebroadcaster_index, cleanup)
{
	lumex::block_rebroadcaster_config config;
	config.rebroadcast_cooldown = 100ms;
	lumex::block_rebroadcaster_index index{ config };

	auto t0 = std::chrono::steady_clock::now ();

	lumex::block_hash hash1{ 1 };
	lumex::block_hash hash2{ 2 };

	// Insert hash1 at T0
	ASSERT_TRUE (index.check_and_record (hash1, t0));

	// Insert hash2 at T0 + 50ms
	ASSERT_TRUE (index.check_and_record (hash2, t0 + 50ms));

	ASSERT_EQ (2, index.size ());

	// Cleanup at T0 + 101ms (hash1 is older than cooldown, hash2 is not)
	auto erased = index.cleanup (t0 + 101ms);
	ASSERT_EQ (1, erased);
	ASSERT_EQ (1, index.size ());

	ASSERT_FALSE (index.contains (hash1)); // Cleaned up
	ASSERT_TRUE (index.contains (hash2)); // Still within cooldown window
}

TEST (block_rebroadcaster_index, cleanup_empty)
{
	lumex::block_rebroadcaster_config config;
	lumex::block_rebroadcaster_index index{ config };

	// Cleanup on empty index should return 0 and not crash
	auto erased = index.cleanup ();
	ASSERT_EQ (0, erased);
	ASSERT_EQ (0, index.size ());
}

/*
 * block_rebroadcaster
 */

TEST (block_rebroadcaster, basic_operation)
{
	lumex::test::system system;
	auto & node = *system.add_node ();

	// Create a block
	lumex::keypair key;
	auto send = lumex::state_block_builder ()
				.account (lumex::dev::genesis_key.pub)
				.previous (lumex::dev::genesis->hash ())
				.representative (lumex::dev::genesis_key.pub)
				.balance (lumex::dev::constants.genesis_amount - lumex::lumex_ratio)
				.link (key.pub)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*system.work.generate (lumex::dev::genesis->hash ()))
				.build ();

	// Process the block
	ASSERT_EQ (lumex::block_status::progress, node.process (send));

	// Start an election - this should trigger block_rebroadcaster
	node.start_election (send);
	ASSERT_TIMELY (5s, node.active.active (*send));

	// Verify it was queued for rebroadcast
	ASSERT_TIMELY (5s, node.stats.count (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::queued) >= 1);
}

TEST (block_rebroadcaster, duplicate_block)
{
	lumex::test::system system;
	auto & node = *system.add_node ();

	// Create a block
	lumex::keypair key;
	auto send = lumex::state_block_builder ()
				.account (lumex::dev::genesis_key.pub)
				.previous (lumex::dev::genesis->hash ())
				.representative (lumex::dev::genesis_key.pub)
				.balance (lumex::dev::constants.genesis_amount - lumex::lumex_ratio)
				.link (key.pub)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*system.work.generate (lumex::dev::genesis->hash ()))
				.build ();

	// Manually push the same block twice
	ASSERT_TRUE (node.block_rebroadcaster.push (send));
	ASSERT_FALSE (node.block_rebroadcaster.push (send)); // Should reject duplicate

	ASSERT_EQ (node.stats.count (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::queued), 1);
}

TEST (block_rebroadcaster, disabled)
{
	lumex::test::system system;
	lumex::node_config config = system.default_config ();
	config.block_rebroadcaster->enable = false;
	auto & node = *system.add_node (config);

	// Create a block
	lumex::keypair key;
	auto send = lumex::state_block_builder ()
				.account (lumex::dev::genesis_key.pub)
				.previous (lumex::dev::genesis->hash ())
				.representative (lumex::dev::genesis_key.pub)
				.balance (lumex::dev::constants.genesis_amount - lumex::lumex_ratio)
				.link (key.pub)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*system.work.generate (lumex::dev::genesis->hash ()))
				.build ();

	// Push should fail when disabled
	ASSERT_FALSE (node.block_rebroadcaster.push (send));
}

// Verify blocks are propagated from Node A to Node B via block_rebroadcaster when election starts
TEST (block_rebroadcaster, propagates_to_peer)
{
	lumex::test::system system;

	// Configure nodes to isolate block_rebroadcaster as the only broadcasting mechanism
	lumex::node_config config = system.default_config ();
	config.bootstrap->enable = false;
	config.vote_rebroadcaster->enable = false;
	config.local_block_broadcaster->enable = false;

	auto & node_a = *system.add_node (config);
	auto & node_b = *system.add_node (config);

	// Ensure nodes are connected
	ASSERT_TIMELY_EQ (5s, node_a.network.size (), 1);
	ASSERT_TIMELY_EQ (5s, node_b.network.size (), 1);

	// Create a block
	lumex::keypair key;
	auto send = lumex::state_block_builder ()
				.account (lumex::dev::genesis_key.pub)
				.previous (lumex::dev::genesis->hash ())
				.representative (lumex::dev::genesis_key.pub)
				.balance (lumex::dev::constants.genesis_amount - lumex::lumex_ratio)
				.link (key.pub)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*system.work.generate (lumex::dev::genesis->hash ()))
				.build ();

	// Process the block on node_a only
	ASSERT_EQ (lumex::block_status::progress, node_a.process (send));

	// Start election - this triggers block_rebroadcaster via election_started callback
	node_a.start_election (send);
	ASSERT_TIMELY (5s, node_a.active.active (*send));

	// Verify block was queued for rebroadcast
	ASSERT_TIMELY (5s, node_a.stats.count (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::queued) >= 1);

	// Verify a single rebroadcast occurred
	ASSERT_TIMELY_EQ (5s, node_a.stats.count (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::rebroadcast), 1);
	ASSERT_ALWAYS_EQ (1s, node_a.stats.count (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::rebroadcast), 1);

	// Verify node_b received the block via publish message
	ASSERT_TIMELY (10s, node_b.stats.count (lumex::stat::type::message, lumex::stat::detail::publish, lumex::stat::dir::in) >= 1);

	// Verify node_b has the block in its ledger
	ASSERT_TIMELY (10s, node_b.block (send->hash ()) != nullptr);
}

// Verify cooldown prevents duplicate rebroadcasts, but allows rebroadcast after expiry
TEST (block_rebroadcaster, cooldown)
{
	lumex::test::system system;

	lumex::node_config config = system.default_config ();
	config.block_rebroadcaster->rebroadcast_cooldown = 500ms;
	config.bootstrap->enable = false;
	config.vote_rebroadcaster->enable = false;
	config.local_block_broadcaster->enable = false;

	auto & node = *system.add_node (config);

	// Add a peer node with rebroadcaster disabled to avoid interference
	lumex::node_config peer_config = system.default_config ();
	peer_config.block_rebroadcaster->enable = false;
	peer_config.bootstrap->enable = false;
	peer_config.vote_rebroadcaster->enable = false;
	peer_config.local_block_broadcaster->enable = false;
	auto & peer_node = *system.add_node (peer_config);

	// Create a block
	lumex::keypair key;
	auto send = lumex::state_block_builder ()
				.account (lumex::dev::genesis_key.pub)
				.previous (lumex::dev::genesis->hash ())
				.representative (lumex::dev::genesis_key.pub)
				.balance (lumex::dev::constants.genesis_amount - lumex::lumex_ratio)
				.link (key.pub)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*system.work.generate (lumex::dev::genesis->hash ()))
				.build ();

	// First push and wait for rebroadcast
	ASSERT_TRUE (node.block_rebroadcaster.push (send));
	ASSERT_TIMELY_EQ (5s, node.stats.count (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::rebroadcast), 1);

	// Verify block propagated to peer
	ASSERT_TIMELY (5s, peer_node.block (send->hash ()) != nullptr);

	// Push same block again immediately (within cooldown)
	ASSERT_TRUE (node.block_rebroadcaster.push (send));

	// Second attempt should be blocked by cooldown
	ASSERT_TIMELY (5s, node.stats.count (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::already_rebroadcasted) >= 1);

	// Wait for cooldown to expire
	WAIT (600ms);

	// Verify rebroadcast count is still 1
	ASSERT_EQ (node.stats.count (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::rebroadcast), 1);

	// Push after cooldown should trigger another rebroadcast
	ASSERT_TRUE (node.block_rebroadcaster.push (send));
	ASSERT_TIMELY_EQ (5s, node.stats.count (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::rebroadcast), 2);
}

// Verify blocks are rebroadcast when elections become stale
TEST (block_rebroadcaster, stale_election_rebroadcasts)
{
	lumex::test::system system;

	lumex::node_config config = system.default_config ();
	config.block_rebroadcaster->rebroadcast_cooldown = 100ms; // Short cooldown to allow second rebroadcast
	config.bootstrap->enable = false;
	config.vote_rebroadcaster->enable = false;
	config.local_block_broadcaster->enable = false;

	auto & node = *system.add_node (config);

	// Add a peer node with rebroadcaster disabled to avoid interference
	lumex::node_config peer_config = system.default_config ();
	peer_config.block_rebroadcaster->enable = false;
	peer_config.bootstrap->enable = false;
	peer_config.vote_rebroadcaster->enable = false;
	peer_config.local_block_broadcaster->enable = false;
	auto & peer_node = *system.add_node (peer_config);

	// Create a block
	lumex::keypair key;
	auto send = lumex::state_block_builder ()
				.account (lumex::dev::genesis_key.pub)
				.previous (lumex::dev::genesis->hash ())
				.representative (lumex::dev::genesis_key.pub)
				.balance (lumex::dev::constants.genesis_amount - lumex::lumex_ratio)
				.link (key.pub)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*system.work.generate (lumex::dev::genesis->hash ()))
				.build ();

	// Process the block
	ASSERT_EQ (lumex::block_status::progress, node.process (send));

	// Start election - this triggers first rebroadcast via election_started callback
	node.start_election (send);
	ASSERT_TIMELY (5s, node.active.active (*send));

	// Verify first rebroadcast from election_started
	ASSERT_TIMELY (5s, node.stats.count (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::queued) >= 1);
	ASSERT_TIMELY_EQ (5s, node.stats.count (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::rebroadcast), 1);

	// Verify block propagated to peer
	ASSERT_TIMELY (5s, peer_node.block (send->hash ()) != nullptr);

	// Wait for election to become stale (default stale_threshold is 1s in dev mode)
	ASSERT_TIMELY (5s, node.stats.count (lumex::stat::type::active_elections, lumex::stat::detail::stale) >= 1);

	// Stale event triggers another push to rebroadcaster
	// With short cooldown, a second rebroadcast should occur
	ASSERT_TIMELY (5s, node.stats.count (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::rebroadcast) >= 2);
}
