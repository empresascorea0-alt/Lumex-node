#include <lumex/lib/blockbuilders.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/store/ledger/pending.hpp>
#include <lumex/store/ledger_store.hpp>
#include <lumex/test_common/system.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

// this test sends 3 send blocks in 3 different epochs and checks that
// the pending table records the epochs correctly for each send
TEST (receivable, pending_table_query_epochs)
{
	lumex::test::system system{ 1 };
	auto & node = *system.nodes[0];
	lumex::keypair key2;
	lumex::block_builder builder;

	// epoch 0 send
	auto send0 = builder
				 .send ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (lumex::dev::constants.genesis_amount - 1)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();
	lumex::test::process (node, { send0 });
	ASSERT_TIMELY (5s, lumex::test::exists (node, { send0 }));

	auto epoch1 = system.upgrade_genesis_epoch (node, lumex::epoch::epoch_1);
	ASSERT_TRUE (epoch1);
	ASSERT_TIMELY (5s, lumex::test::exists (node, { epoch1 }));

	// epoch 1 send
	auto send1 = builder
				 .state ()
				 .account (lumex::dev::genesis_key.pub)
				 .representative (lumex::dev::genesis_key.pub)
				 .previous (epoch1->hash ())
				 .link (key2.pub)
				 .balance (lumex::dev::constants.genesis_amount - 11)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (epoch1->hash ()))
				 .build ();
	ASSERT_TRUE (lumex::test::process (node, { send1 }));
	ASSERT_TIMELY (5s, lumex::test::exists (node, { send1 }));

	auto epoch2 = system.upgrade_genesis_epoch (node, lumex::epoch::epoch_2);
	ASSERT_TRUE (epoch2);
	ASSERT_TIMELY (5s, lumex::test::exists (node, { epoch2 }));

	// epoch 2 send
	auto send2 = builder
				 .state ()
				 .account (lumex::dev::genesis_key.pub)
				 .representative (lumex::dev::genesis_key.pub)
				 .previous (epoch2->hash ())
				 .link (key2.pub)
				 .balance (lumex::dev::constants.genesis_amount - 111)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (epoch2->hash ()))
				 .build ();
	lumex::test::process (node, { send2 });
	ASSERT_TIMELY (5s, lumex::test::exists (node, { send2 }));

	auto tx = node.store.tx_begin_read ();

	// check epoch 0 send
	{
		lumex::pending_key key{ key2.pub, send0->hash () };
		auto opt_info = node.store.pending.get (tx, key);
		ASSERT_TRUE (opt_info.has_value ());
		auto info = opt_info.value ();
		ASSERT_EQ (info.source, lumex::dev::genesis_key.pub);
		ASSERT_EQ (info.amount, 1);
		ASSERT_EQ (info.epoch, lumex::epoch::epoch_0);
	}

	// check epoch 1 send
	{
		lumex::pending_key key{ key2.pub, send1->hash () };
		auto opt_info = node.store.pending.get (tx, key);
		ASSERT_TRUE (opt_info.has_value ());
		auto info = opt_info.value ();
		ASSERT_EQ (info.source, lumex::dev::genesis_key.pub);
		ASSERT_EQ (info.amount, 10);
		ASSERT_EQ (info.epoch, lumex::epoch::epoch_1);
	}

	// check epoch 2 send
	{
		lumex::pending_key key{ key2.pub, send2->hash () };
		auto opt_info = node.store.pending.get (tx, key);
		ASSERT_TRUE (opt_info.has_value ());
		auto info = opt_info.value ();
		ASSERT_EQ (info.source, lumex::dev::genesis_key.pub);
		ASSERT_EQ (info.amount, 100);
		ASSERT_EQ (info.epoch, lumex::epoch::epoch_2);
	}
}