#include <lumex/lib/blockbuilders.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/thread_runner.hpp>
#include <lumex/lib/work_version.hpp>
#include <lumex/messages/keepalive.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/transport/inproc.hpp>
#include <lumex/node/wallet.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_set_any.hpp>
#include <lumex/store/ledger/account.hpp>
#include <lumex/test_common/network.hpp>
#include <lumex/test_common/system.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (system, work_generate_limited)
{
	lumex::test::system system;
	lumex::block_hash key (1);
	auto min = lumex::dev::network_params.work.entry;
	auto max = lumex::dev::network_params.work.base;
	for (int i = 0; i < 5; ++i)
	{
		auto work = system.work_generate_limited (key, min, max);
		auto difficulty = lumex::dev::network_params.work.difficulty (lumex::work_version::work_1, key, work);
		ASSERT_GE (difficulty, min);
		ASSERT_LT (difficulty, max);
	}
}

// All nodes in the system should agree on the genesis balance
TEST (system, system_genesis)
{
	lumex::test::system system (2);
	for (auto & i : system.nodes)
	{
		auto transaction = i->ledger.tx_begin_read ();
		ASSERT_EQ (lumex::dev::constants.genesis_amount, i->ledger.any.account_balance (transaction, lumex::dev::genesis_key.pub));
	}
}

TEST (system, DISABLED_generate_send_existing)
{
	lumex::test::system system (1);
	auto & node1 (*system.nodes[0]);
	lumex::thread_runner runner (system.io_ctx, system.logger, node1.config.io_threads);
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	lumex::keypair stake_preserver;
	auto send_block (system.wallet (0)->send_action (lumex::dev::genesis_key.pub, stake_preserver.pub, lumex::dev::constants.genesis_amount / 3 * 2, true));
	auto info1 = node1.ledger.any.account_get (node1.ledger.tx_begin_read (), lumex::dev::genesis_key.pub);
	ASSERT_TRUE (info1);
	std::vector<lumex::account> accounts;
	accounts.push_back (lumex::dev::genesis_key.pub);
	system.generate_send_existing (node1, accounts);
	// Have stake_preserver receive funds after generate_send_existing so it isn't chosen as the destination
	{
		auto transaction = node1.ledger.tx_begin_write ();
		lumex::block_builder builder;
		auto open_block = builder
						  .open ()
						  .source (send_block->hash ())
						  .representative (lumex::dev::genesis_key.pub)
						  .account (stake_preserver.pub)
						  .sign (stake_preserver.prv, stake_preserver.pub)
						  .work (0)
						  .build ();
		node1.work_generate_blocking (*open_block);
		ASSERT_EQ (lumex::block_status::progress, node1.ledger.process (transaction, open_block));
	}
	ASSERT_GT (node1.balance (stake_preserver.pub), node1.balance (lumex::dev::genesis_key.pub));
	auto info2 = node1.ledger.any.account_get (node1.ledger.tx_begin_read (), lumex::dev::genesis_key.pub);
	ASSERT_TRUE (info2);
	ASSERT_NE (info1->head, info2->head);
	system.deadline_set (15s);
	while (info2->block_count < info1->block_count + 2)
	{
		ASSERT_NO_ERROR (system.poll ());
		auto transaction = node1.ledger.tx_begin_read ();
		info2 = node1.ledger.any.account_get (transaction, lumex::dev::genesis_key.pub);
		ASSERT_TRUE (info2);
	}
	ASSERT_EQ (info1->block_count + 2, info2->block_count);
	ASSERT_EQ (info2->balance, lumex::dev::constants.genesis_amount / 3);
	{
		ASSERT_NE (node1.ledger.any.block_amount (node1.ledger.tx_begin_read (), info2->head), 0);
	}
	system.stop ();
	runner.join ();
}

TEST (system, DISABLED_generate_send_new)
{
	lumex::test::system system (1);
	auto & node1 (*system.nodes[0]);
	lumex::thread_runner runner (system.io_ctx, system.logger, node1.config.io_threads);
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	{
		auto transaction (node1.store.tx_begin_read ());
		auto iterator1 (node1.store.account.begin (transaction));
		ASSERT_NE (node1.store.account.end (transaction), iterator1);
		++iterator1;
		ASSERT_EQ (node1.store.account.end (transaction), iterator1);
	}
	lumex::keypair stake_preserver;
	auto send_block (system.wallet (0)->send_action (lumex::dev::genesis_key.pub, stake_preserver.pub, lumex::dev::constants.genesis_amount / 3 * 2, true));
	{
		auto transaction = node1.ledger.tx_begin_write ();
		lumex::block_builder builder;
		auto open_block = builder
						  .open ()
						  .source (send_block->hash ())
						  .representative (lumex::dev::genesis_key.pub)
						  .account (stake_preserver.pub)
						  .sign (stake_preserver.prv, stake_preserver.pub)
						  .work (0)
						  .build ();
		node1.work_generate_blocking (*open_block);
		ASSERT_EQ (lumex::block_status::progress, node1.ledger.process (transaction, open_block));
	}
	ASSERT_GT (node1.balance (stake_preserver.pub), node1.balance (lumex::dev::genesis_key.pub));
	std::vector<lumex::account> accounts;
	accounts.push_back (lumex::dev::genesis_key.pub);
	// This indirectly waits for online weight to stabilize, required to prevent intermittent failures
	ASSERT_TIMELY (5s, node1.wallets.reps ().voting > 0);
	system.generate_send_new (node1, accounts);
	lumex::account new_account{};
	{
		auto wallet_accounts = system.wallet (0)->accounts ();
		ASSERT_EQ (2, wallet_accounts.size ());
		for (auto const & acc : wallet_accounts)
		{
			if (acc != lumex::dev::genesis_key.pub)
			{
				new_account = acc;
			}
		}
		ASSERT_FALSE (new_account.is_zero ());
	}
	ASSERT_TIMELY (10s, node1.balance (new_account) != 0);
	system.stop ();
	runner.join ();
}

TEST (system, rep_initialize_one)
{
	lumex::test::system system;
	lumex::keypair key;
	system.ledger_initialization_set ({ key });
	auto node = system.add_node ();
	ASSERT_EQ (lumex::dev::constants.genesis_amount, node->balance (key.pub));
}

TEST (system, rep_initialize_two)
{
	lumex::test::system system;
	lumex::keypair key0;
	lumex::keypair key1;
	system.ledger_initialization_set ({ key0, key1 });
	auto node = system.add_node ();
	ASSERT_EQ (lumex::dev::constants.genesis_amount / 2, node->balance (key0.pub));
	ASSERT_EQ (lumex::dev::constants.genesis_amount / 2, node->balance (key1.pub));
}

TEST (system, rep_initialize_one_reserve)
{
	lumex::test::system system;
	lumex::keypair key;
	system.ledger_initialization_set ({ key }, lumex::Klumex_ratio);
	auto node = system.add_node ();
	ASSERT_EQ (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio, node->balance (key.pub));
	ASSERT_EQ (lumex::Klumex_ratio, node->balance (lumex::dev::genesis_key.pub));
}

TEST (system, rep_initialize_two_reserve)
{
	lumex::test::system system;
	lumex::keypair key0;
	lumex::keypair key1;
	system.ledger_initialization_set ({ key0, key1 }, lumex::Klumex_ratio);
	auto node = system.add_node ();
	ASSERT_EQ ((lumex::dev::constants.genesis_amount - lumex::Klumex_ratio) / 2, node->balance (key0.pub));
	ASSERT_EQ ((lumex::dev::constants.genesis_amount - lumex::Klumex_ratio) / 2, node->balance (key1.pub));
}

TEST (system, rep_initialize_many)
{
	lumex::test::system system;
	lumex::keypair key0;
	lumex::keypair key1;
	system.ledger_initialization_set ({ key0, key1 }, lumex::Klumex_ratio);
	auto node0 = system.add_node ();
	ASSERT_EQ ((lumex::dev::constants.genesis_amount - lumex::Klumex_ratio) / 2, node0->balance (key0.pub));
	ASSERT_EQ ((lumex::dev::constants.genesis_amount - lumex::Klumex_ratio) / 2, node0->balance (key1.pub));
	auto node1 = system.add_node ();
	ASSERT_EQ ((lumex::dev::constants.genesis_amount - lumex::Klumex_ratio) / 2, node1->balance (key0.pub));
	ASSERT_EQ ((lumex::dev::constants.genesis_amount - lumex::Klumex_ratio) / 2, node1->balance (key1.pub));
}

TEST (system, transport_basic)
{
	lumex::test::system system{ 1 };
	auto & node0 = *system.nodes[0];
	// Start nodes in separate systems so they don't automatically connect with each other.
	lumex::test::system system1{ 1 };
	auto & node1 = *system1.nodes[0];
	ASSERT_EQ (0, node1.stats.count (lumex::stat::type::message, lumex::stat::detail::keepalive, lumex::stat::dir::in));
	lumex::transport::inproc::channel channel{ node0, node1 };
	// Send a keepalive message since they are easy to construct
	lumex::messages::keepalive junk{ lumex::dev::network_params.network };
	channel.send (junk, lumex::transport::traffic_type::test);
	// Ensure the keepalive has been reecived on the target.
	ASSERT_TIMELY (5s, node1.stats.count (lumex::stat::type::message, lumex::stat::detail::keepalive, lumex::stat::dir::in) > 0);
}
