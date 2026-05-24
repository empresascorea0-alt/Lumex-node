#include <lumex/lib/blockbuilders.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/backlog_scan.hpp>
#include <lumex/node/block_processor.hpp>
#include <lumex/node/bootstrap/bootstrap_config.hpp>
#include <lumex/node/cementing_set.hpp>
#include <lumex/node/election.hpp>
#include <lumex/node/ledger_notifications.hpp>
#include <lumex/node/make_store.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/unchecked_map.hpp>
#include <lumex/node/wallet.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_set_cemented.hpp>
#include <lumex/test_common/ledger_context.hpp>
#include <lumex/test_common/system.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <latch>

using namespace std::chrono_literals;

namespace
{
struct cementing_set_context
{
	lumex::test::ledger_context & ledger_ctx;

	lumex::logger & logger;
	lumex::stats & stats;
	lumex::ledger & ledger;

	lumex::node_config node_config;
	lumex::ledger_notifications ledger_notifications;
	lumex::cementing_set cementing_set;

	explicit cementing_set_context (lumex::test::ledger_context & ledger_context_a, lumex::node_config node_config_a = {}) :
		ledger_ctx{ ledger_context_a },
		logger{ ledger_ctx.logger () },
		stats{ ledger_ctx.stats () },
		ledger{ ledger_ctx.ledger () },
		node_config{ std::move (node_config_a) },
		ledger_notifications{ node_config, stats, logger },
		cementing_set{ node_config.cementing_set, ledger, ledger_notifications, stats, logger }
	{
	}
};
}

TEST (cementing_set, construction)
{
	auto ledger_ctx = lumex::test::ledger_empty ();
	cementing_set_context ctx{ ledger_ctx };
}

TEST (cementing_set, add_exists)
{
	auto ledger_ctx = lumex::test::ledger_send_receive ();
	cementing_set_context ctx{ ledger_ctx };
	lumex::cementing_set & cementing_set = ctx.cementing_set;
	auto send = ledger_ctx.blocks ()[0];
	cementing_set.add (send->hash ());
	ASSERT_TRUE (cementing_set.contains (send->hash ()));
}

TEST (cementing_set, process_one)
{
	auto ledger_ctx = lumex::test::ledger_send_receive ();
	cementing_set_context ctx{ ledger_ctx };
	lumex::cementing_set & cementing_set = ctx.cementing_set;
	std::atomic<int> count = 0;
	std::mutex mutex;
	std::condition_variable condition;
	cementing_set.cemented_observers.add ([&] (auto const &) { ++count; condition.notify_all (); });
	cementing_set.add (ledger_ctx.blocks ()[0]->hash ());
	lumex::test::start_stop_guard guard{ cementing_set };
	std::unique_lock lock{ mutex };
	ASSERT_TRUE (condition.wait_for (lock, 5s, [&] () { return count == 1; }));
	ASSERT_EQ (1, ctx.stats.count (lumex::stat::type::confirmation_height, lumex::stat::detail::blocks_cemented, lumex::stat::dir::in));
	ASSERT_EQ (2, ctx.ledger.cemented_count ());
}

TEST (cementing_set, process_multiple)
{
	lumex::test::system system;
	auto ledger_ctx = lumex::test::ledger_send_receive ();
	cementing_set_context ctx{ ledger_ctx };
	lumex::cementing_set & cementing_set = ctx.cementing_set;
	std::atomic<int> count = 0;
	std::mutex mutex;
	std::condition_variable condition;
	cementing_set.cemented_observers.add ([&] (auto const &) { ++count; condition.notify_all (); });
	cementing_set.add (ledger_ctx.blocks ()[0]->hash ());
	cementing_set.add (ledger_ctx.blocks ()[1]->hash ());
	lumex::test::start_stop_guard guard{ cementing_set };
	std::unique_lock lock{ mutex };
	ASSERT_TRUE (condition.wait_for (lock, 5s, [&] () { return count == 2; }));
	ASSERT_EQ (2, ctx.stats.count (lumex::stat::type::confirmation_height, lumex::stat::detail::blocks_cemented, lumex::stat::dir::in));
	ASSERT_EQ (3, ctx.ledger.cemented_count ());
}

TEST (confirmation_callback, observer_callbacks)
{
	lumex::test::system system;
	lumex::node_flags node_flags;
	lumex::node_config node_config = system.default_config ();
	node_config.backlog_scan->enable = false;
	auto node = system.add_node (node_config, node_flags);

	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	lumex::block_hash latest (node->latest (lumex::dev::genesis_key.pub));

	lumex::keypair key1;
	lumex::block_builder builder;
	auto send = builder
				.send ()
				.previous (latest)
				.destination (key1.pub)
				.balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*system.work.generate (latest))
				.build ();
	auto send1 = builder
				 .send ()
				 .previous (send->hash ())
				 .destination (key1.pub)
				 .balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio * 2)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (send->hash ()))
				 .build ();

	{
		auto transaction = node->ledger.tx_begin_write ();
		ASSERT_EQ (lumex::block_status::progress, node->ledger.process (transaction, send));
		ASSERT_EQ (lumex::block_status::progress, node->ledger.process (transaction, send1));
	}

	node->cementing_set.add (send1->hash ());

	// Callback is performed for all blocks that are confirmed
	ASSERT_TIMELY_EQ (5s, 2, node->ledger.stats.count (lumex::stat::type::confirmation_observer, lumex::stat::dir::out));

	ASSERT_EQ (2, node->stats.count (lumex::stat::type::confirmation_height, lumex::stat::detail::blocks_cemented, lumex::stat::dir::in));
	ASSERT_EQ (3, node->ledger.cemented_count ());
}

// The callback and confirmation history should only be updated after confirmation height is set (and not just after voting)
TEST (confirmation_callback, confirmed_history)
{
	lumex::test::system system;
	lumex::node_config node_config = system.default_config ();
	node_config.backlog_scan->enable = false;
	node_config.bootstrap->enable = false;
	auto node = system.add_node (node_config);

	lumex::block_hash latest (node->latest (lumex::dev::genesis_key.pub));

	lumex::keypair key1;
	lumex::block_builder builder;
	auto send = builder
				.send ()
				.previous (latest)
				.destination (key1.pub)
				.balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*system.work.generate (latest))
				.build ();
	ASSERT_EQ (lumex::block_status::progress, node->ledger.process (node->ledger.tx_begin_write (), send));

	auto send1 = builder
				 .send ()
				 .previous (send->hash ())
				 .destination (key1.pub)
				 .balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio * 2)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (send->hash ()))
				 .build ();
	ASSERT_EQ (lumex::block_status::progress, node->ledger.process (node->ledger.tx_begin_write (), send1));

	std::shared_ptr<lumex::election> election;
	ASSERT_TIMELY (5s, election = lumex::test::start_election (system, *node, send1->hash ()));
	{
		// The write guard prevents the confirmation height processor doing any writes
		auto write_guard = node->store.write_queue.wait (lumex::store::writer::testing);

		// Confirm send1
		election->force_confirm ();
		ASSERT_TIMELY_EQ (10s, node->active.size (), 0);
		ASSERT_EQ (0, node->active.recently_cemented.size ());
		ASSERT_TRUE (node->active.empty ());

		auto transaction = node->ledger.tx_begin_read ();
		ASSERT_FALSE (node->ledger.cemented.block_exists (transaction, send->hash ()));

		ASSERT_TIMELY (10s, node->store.write_queue.contains (lumex::store::writer::confirmation_height));

		// Confirm that no inactive callbacks have been called when the confirmation height processor has already iterated over it, waiting to write
		ASSERT_ALWAYS_EQ (50ms, 0, node->stats.count (lumex::stat::type::confirmation_observer, lumex::stat::detail::inactive_conf_height, lumex::stat::dir::out));
	}

	ASSERT_TIMELY (10s, !node->store.write_queue.contains (lumex::store::writer::confirmation_height));

	ASSERT_TIMELY (5s, node->ledger.cemented.block_exists (node->ledger.tx_begin_read (), send->hash ()));

	ASSERT_TIMELY_EQ (10s, node->active.size (), 0);
	ASSERT_TIMELY_EQ (10s, node->stats.count (lumex::stat::type::confirmation_observer, lumex::stat::detail::active_quorum, lumex::stat::dir::out), 1);

	// Each block that's confirmed is in the recently_cemented history
	ASSERT_EQ (2, node->active.recently_cemented.size ());
	ASSERT_TRUE (node->active.empty ());

	// Confirm the callback is not called under this circumstance
	ASSERT_TIMELY_EQ (5s, 1, node->stats.count (lumex::stat::type::confirmation_observer, lumex::stat::detail::active_quorum, lumex::stat::dir::out));
	ASSERT_TIMELY_EQ (5s, 1, node->stats.count (lumex::stat::type::confirmation_observer, lumex::stat::detail::inactive_conf_height, lumex::stat::dir::out));
	ASSERT_TIMELY_EQ (5s, 2, node->stats.count (lumex::stat::type::confirmation_height, lumex::stat::detail::blocks_cemented, lumex::stat::dir::in));
	ASSERT_EQ (3, node->ledger.cemented_count ());
}

TEST (confirmation_callback, dependent_election)
{
	lumex::test::system system;
	lumex::node_flags node_flags;
	lumex::node_config node_config = system.default_config ();
	node_config.backlog_scan->enable = false;
	auto node = system.add_node (node_config, node_flags);

	lumex::block_hash latest (node->latest (lumex::dev::genesis_key.pub));

	lumex::keypair key1;
	lumex::block_builder builder;
	auto send = builder
				.send ()
				.previous (latest)
				.destination (key1.pub)
				.balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*system.work.generate (latest))
				.build ();
	auto send1 = builder
				 .send ()
				 .previous (send->hash ())
				 .destination (key1.pub)
				 .balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio * 2)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (send->hash ()))
				 .build ();
	auto send2 = builder
				 .send ()
				 .previous (send1->hash ())
				 .destination (key1.pub)
				 .balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio * 3)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	{
		auto transaction = node->ledger.tx_begin_write ();
		ASSERT_EQ (lumex::block_status::progress, node->ledger.process (transaction, send));
		ASSERT_EQ (lumex::block_status::progress, node->ledger.process (transaction, send1));
		ASSERT_EQ (lumex::block_status::progress, node->ledger.process (transaction, send2));
	}

	// This election should be confirmed as active_conf_height
	ASSERT_TRUE (lumex::test::start_election (system, *node, send1->hash ()));
	// Start an election and confirm it
	auto election = lumex::test::start_election (system, *node, send2->hash ());
	ASSERT_NE (nullptr, election);
	election->force_confirm ();

	// Wait for blocks to be confirmed in ledger, callbacks will happen after
	ASSERT_TIMELY_EQ (5s, 3, node->stats.count (lumex::stat::type::confirmation_height, lumex::stat::detail::blocks_cemented, lumex::stat::dir::in));
	// Once the item added to the confirming set no longer exists, callbacks have completed
	ASSERT_TIMELY (5s, !node->cementing_set.contains (send2->hash ()));

	ASSERT_TIMELY_EQ (5s, 1, node->stats.count (lumex::stat::type::confirmation_observer, lumex::stat::detail::active_quorum, lumex::stat::dir::out));
	ASSERT_TIMELY_EQ (5s, 1, node->stats.count (lumex::stat::type::confirmation_observer, lumex::stat::detail::active_conf_height, lumex::stat::dir::out));
	ASSERT_TIMELY_EQ (5s, 1, node->stats.count (lumex::stat::type::confirmation_observer, lumex::stat::detail::inactive_conf_height, lumex::stat::dir::out));
	ASSERT_EQ (4, node->ledger.cemented_count ());
}
