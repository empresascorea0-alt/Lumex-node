#include <lumex/crypto_lib/random_pool.hpp>
#include <lumex/lib/block_type.hpp>
#include <lumex/lib/blockbuilders.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/files.hpp>
#include <lumex/lib/lmdbconfig.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/ratios.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/lib/work.hpp>
#include <lumex/node/endpoint.hpp>
#include <lumex/node/unchecked_map.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_set_any.hpp>
#include <lumex/store/db_val.hpp>
#include <lumex/store/db_val_templ.hpp>
#include <lumex/store/ledger/account.hpp>
#include <lumex/store/ledger/block.hpp>
#include <lumex/store/ledger/confirmation_height.hpp>
#include <lumex/store/ledger/final_vote.hpp>
#include <lumex/store/ledger/online_weight.hpp>
#include <lumex/store/ledger/peer.hpp>
#include <lumex/store/ledger/pending.hpp>
#include <lumex/store/ledger/pruned.hpp>
#include <lumex/store/ledger/successor.hpp>
#include <lumex/store/ledger/topology.hpp>
#include <lumex/store/ledger/version.hpp>
#include <lumex/store/ledger_store.hpp>
#include <lumex/store/lmdb/backend_lmdb.hpp>
#include <lumex/store/rocksdb/backend_rocksdb.hpp>
#include <lumex/store/versioning.hpp>
#include <lumex/test_common/make_store.hpp>
#include <lumex/test_common/system.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <unordered_set>
#include <vector>

using namespace std::chrono_literals;

TEST (block_store, construction)
{
	auto store = lumex::test::make_store ();
}

TEST (block_store, block_details)
{
	lumex::block_details details_send (lumex::epoch::epoch_0, true, false, false);
	ASSERT_TRUE (details_send.is_send);
	ASSERT_FALSE (details_send.is_receive);
	ASSERT_FALSE (details_send.is_epoch);
	ASSERT_EQ (lumex::epoch::epoch_0, details_send.epoch);

	lumex::block_details details_receive (lumex::epoch::epoch_1, false, true, false);
	ASSERT_FALSE (details_receive.is_send);
	ASSERT_TRUE (details_receive.is_receive);
	ASSERT_FALSE (details_receive.is_epoch);
	ASSERT_EQ (lumex::epoch::epoch_1, details_receive.epoch);

	lumex::block_details details_epoch (lumex::epoch::epoch_2, false, false, true);
	ASSERT_FALSE (details_epoch.is_send);
	ASSERT_FALSE (details_epoch.is_receive);
	ASSERT_TRUE (details_epoch.is_epoch);
	ASSERT_EQ (lumex::epoch::epoch_2, details_epoch.epoch);

	lumex::block_details details_none (lumex::epoch::unspecified, false, false, false);
	ASSERT_FALSE (details_none.is_send);
	ASSERT_FALSE (details_none.is_receive);
	ASSERT_FALSE (details_none.is_epoch);
	ASSERT_EQ (lumex::epoch::unspecified, details_none.epoch);
}

TEST (block_store, block_details_serialization)
{
	lumex::block_details details1;
	details1.epoch = lumex::epoch::epoch_2;
	details1.is_epoch = false;
	details1.is_receive = true;
	details1.is_send = false;
	std::vector<uint8_t> vector;
	{
		lumex::vectorstream stream1 (vector);
		details1.serialize (stream1);
	}
	lumex::bufferstream stream2 (vector.data (), vector.size ());
	lumex::block_details details2;
	ASSERT_FALSE (details2.deserialize (stream2));
	ASSERT_EQ (details1, details2);
}

TEST (block_store, sideband_serialization)
{
	lumex::block_sideband sideband1;
	sideband1.account = 1;
	sideband1.balance = 2;
	sideband1.height = 3;
	sideband1.timestamp = 5;
	sideband1.topo_height = 42;
	std::vector<uint8_t> vector;
	{
		lumex::vectorstream stream1 (vector);
		sideband1.serialize (stream1, lumex::block_type::receive);
	}
	lumex::bufferstream stream2 (vector.data (), vector.size ());
	lumex::block_sideband sideband2;
	ASSERT_FALSE (sideband2.deserialize (stream2, lumex::block_type::receive));
	ASSERT_EQ (sideband1.account, sideband2.account);
	ASSERT_EQ (sideband1.balance, sideband2.balance);
	ASSERT_EQ (sideband1.height, sideband2.height);
	ASSERT_EQ (sideband1.timestamp, sideband2.timestamp);
	ASSERT_EQ (sideband1.topo_height, sideband2.topo_height);
}

TEST (block_store, add_item)
{
	auto store = lumex::test::make_store ();

	lumex::block_builder builder;
	auto block = builder
				 .open ()
				 .source (0)
				 .representative (1)
				 .account (0)
				 .sign (lumex::keypair ().prv, 0)
				 .work (0)
				 .build ();
	block->sideband_set ({});
	auto hash1 (block->hash ());
	auto transaction (store->tx_begin_write ());
	auto latest1 (store->block.get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	ASSERT_FALSE (store->block.exists (transaction, hash1));
	store->block.put (transaction, hash1, *block);
	auto latest2 (store->block.get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (*block, *latest2);
	ASSERT_TRUE (store->block.exists (transaction, hash1));
	ASSERT_FALSE (store->block.exists (transaction, hash1.number () - 1));
	store->block.del (transaction, hash1);
	auto latest3 (store->block.get (transaction, hash1));
	ASSERT_EQ (nullptr, latest3);
}

TEST (block_store, clear_successor)
{
	auto store = lumex::test::make_store ();

	lumex::block_builder builder;
	auto block1 = builder
				  .open ()
				  .source (0)
				  .representative (1)
				  .account (0)
				  .sign (lumex::keypair ().prv, 0)
				  .work (0)
				  .build ();
	block1->sideband_set ({});
	auto transaction (store->tx_begin_write ());
	store->block.put (transaction, block1->hash (), *block1);
	// Open block has no predecessor, so no successor entry
	ASSERT_FALSE (store->successor.get (transaction, block1->hash ()).has_value ());
	// Manually set a successor via the successor table
	auto block1_hash = block1->hash ();
	lumex::block_hash fake_successor{ 42 };
	store->successor.put (transaction, block1_hash, fake_successor);
	{
		auto result = store->successor.get (transaction, block1_hash);
		ASSERT_TRUE (result.has_value ());
		ASSERT_EQ (fake_successor, *result);
	}
	store->successor.del (transaction, block1_hash);
	{
		auto result = store->successor.get (transaction, block1_hash);
		ASSERT_FALSE (result.has_value ());
	}
}

TEST (block_store, add_nonempty_block)
{
	auto store = lumex::test::make_store ();

	lumex::keypair key1;
	lumex::block_builder builder;
	auto block = builder
				 .open ()
				 .source (0)
				 .representative (1)
				 .account (0)
				 .sign (lumex::keypair ().prv, 0)
				 .work (0)
				 .build ();
	block->sideband_set ({});
	auto hash1 (block->hash ());
	block->signature = lumex::sign_message (key1.prv, key1.pub, hash1);
	auto transaction (store->tx_begin_write ());
	auto latest1 (store->block.get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	store->block.put (transaction, hash1, *block);
	auto latest2 (store->block.get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (*block, *latest2);
}

TEST (block_store, add_two_items)
{
	auto store = lumex::test::make_store ();

	lumex::keypair key1;
	lumex::block_builder builder;
	auto block = builder
				 .open ()
				 .source (0)
				 .representative (1)
				 .account (1)
				 .sign (lumex::keypair ().prv, 0)
				 .work (0)
				 .build ();
	block->sideband_set ({});
	auto hash1 (block->hash ());
	block->signature = lumex::sign_message (key1.prv, key1.pub, hash1);
	auto transaction (store->tx_begin_write ());
	auto latest1 (store->block.get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	auto block2 = builder
				  .open ()
				  .source (0)
				  .representative (1)
				  .account (3)
				  .sign (lumex::keypair ().prv, 0)
				  .work (0)
				  .build ();
	block2->sideband_set ({});
	block2->hashables.account = 3;
	auto hash2 (block2->hash ());
	block2->signature = lumex::sign_message (key1.prv, key1.pub, hash2);
	auto latest2 (store->block.get (transaction, hash2));
	ASSERT_EQ (nullptr, latest2);
	store->block.put (transaction, hash1, *block);
	store->block.put (transaction, hash2, *block2);
	auto latest3 (store->block.get (transaction, hash1));
	ASSERT_NE (nullptr, latest3);
	ASSERT_EQ (*block, *latest3);
	auto latest4 (store->block.get (transaction, hash2));
	ASSERT_NE (nullptr, latest4);
	ASSERT_EQ (*block2, *latest4);
	ASSERT_FALSE (*latest3 == *latest4);
}

TEST (block_store, add_receive)
{
	auto store = lumex::test::make_store ();

	lumex::keypair key1;
	lumex::keypair key2;
	lumex::block_builder builder;
	auto block1 = builder
				  .open ()
				  .source (0)
				  .representative (1)
				  .account (0)
				  .sign (lumex::keypair ().prv, 0)
				  .work (0)
				  .build ();
	block1->sideband_set ({});
	auto transaction (store->tx_begin_write ());
	store->block.put (transaction, block1->hash (), *block1);
	auto block = builder
				 .receive ()
				 .previous (block1->hash ())
				 .source (1)
				 .sign (lumex::keypair ().prv, 2)
				 .work (3)
				 .build ();
	block->sideband_set ({});
	lumex::block_hash hash1 (block->hash ());
	auto latest1 (store->block.get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	store->block.put (transaction, hash1, *block);
	auto latest2 (store->block.get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (*block, *latest2);
}

TEST (block_store, add_pending)
{
	auto store = lumex::test::make_store ();

	lumex::keypair key1;
	lumex::pending_key key2 (0, 0);
	auto transaction (store->tx_begin_write ());
	ASSERT_FALSE (store->pending.get (transaction, key2));
	lumex::pending_info pending1;
	store->pending.put (transaction, key2, pending1);
	std::optional<lumex::pending_info> pending2;
	ASSERT_TRUE (pending2 = store->pending.get (transaction, key2));
	ASSERT_EQ (pending1, pending2);
	store->pending.del (transaction, key2);
	ASSERT_FALSE (store->pending.get (transaction, key2));
}

TEST (block_store, pending_iterator)
{
	auto store = lumex::test::make_store ();
	auto transaction (store->tx_begin_write ());
	ASSERT_EQ (store->pending.end (transaction), store->pending.begin (transaction));
	store->pending.put (transaction, lumex::pending_key (1, 2), { 2, 3, lumex::epoch::epoch_1 });
	auto current (store->pending.begin (transaction));
	ASSERT_NE (store->pending.end (transaction), current);
	lumex::pending_key key1 (current->first);
	ASSERT_EQ (lumex::account (1), key1.account);
	ASSERT_EQ (lumex::block_hash (2), key1.hash);
	lumex::pending_info pending (current->second);
	ASSERT_EQ (lumex::account (2), pending.source);
	ASSERT_EQ (lumex::amount (3), pending.amount);
	ASSERT_EQ (lumex::epoch::epoch_1, pending.epoch);
}

/**
 * Regression test for Issue 1164
 * This reconstructs the situation where a key is larger in pending than the account being iterated in pending_v1, leaving
 * iteration order up to the value, causing undefined behavior.
 * After the bugfix, the value is compared only if the keys are equal.
 */
TEST (block_store, pending_iterator_comparison)
{
	lumex::test::system system;
	auto store = lumex::test::make_store ();

	auto transaction (store->tx_begin_write ());
	// Populate pending
	store->pending.put (transaction, lumex::pending_key (lumex::account (3), lumex::block_hash (1)), lumex::pending_info (lumex::account (10), lumex::amount (1), lumex::epoch::epoch_0));
	store->pending.put (transaction, lumex::pending_key (lumex::account (3), lumex::block_hash (4)), lumex::pending_info (lumex::account (10), lumex::amount (0), lumex::epoch::epoch_0));
	// Populate pending_v1
	store->pending.put (transaction, lumex::pending_key (lumex::account (2), lumex::block_hash (2)), lumex::pending_info (lumex::account (10), lumex::amount (2), lumex::epoch::epoch_1));
	store->pending.put (transaction, lumex::pending_key (lumex::account (2), lumex::block_hash (3)), lumex::pending_info (lumex::account (10), lumex::amount (3), lumex::epoch::epoch_1));

	// Iterate account 3 (pending)
	{
		size_t count = 0;
		lumex::account begin (3);
		lumex::account end (begin.number () + 1);
		for (auto i (store->pending.begin (transaction, lumex::pending_key (begin, 0))), n (store->pending.begin (transaction, lumex::pending_key (end, 0))); i != n; ++i, ++count)
		{
			lumex::pending_key key (i->first);
			ASSERT_EQ (key.account, begin);
			ASSERT_LT (count, 3);
		}
		ASSERT_EQ (count, 2);
	}

	// Iterate account 2 (pending_v1)
	{
		size_t count = 0;
		lumex::account begin (2);
		lumex::account end (begin.number () + 1);
		for (auto i (store->pending.begin (transaction, lumex::pending_key (begin, 0))), n (store->pending.begin (transaction, lumex::pending_key (end, 0))); i != n; ++i, ++count)
		{
			lumex::pending_key key (i->first);
			ASSERT_EQ (key.account, begin);
			ASSERT_LT (count, 3);
		}
		ASSERT_EQ (count, 2);
	}
}

TEST (block_store, genesis)
{
	auto store = lumex::test::make_store ();
	auto transaction (store->tx_begin_write ());
	lumex::ledger::seed_genesis (*store, transaction, lumex::dev::constants);
	lumex::account_info info;
	ASSERT_FALSE (store->account.get (transaction, lumex::dev::genesis_key.pub, info));
	ASSERT_EQ (lumex::dev::genesis->hash (), info.head);
	auto block1 (store->block.get (transaction, info.head));
	ASSERT_NE (nullptr, block1);
	auto receive1 (dynamic_cast<lumex::open_block *> (block1.get ()));
	ASSERT_NE (nullptr, receive1);
	ASSERT_LE (info.modified, lumex::seconds_since_epoch ());
	ASSERT_EQ (info.block_count, 1);
	// Genesis block should be confirmed by default
	lumex::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store->confirmation_height.get (transaction, lumex::dev::genesis_key.pub, confirmation_height_info));
	ASSERT_EQ (confirmation_height_info.height, 1);
	ASSERT_EQ (confirmation_height_info.frontier, lumex::dev::genesis->hash ());
	auto dev_pub_text (lumex::dev::genesis_key.pub.to_string ());
	auto dev_pub_account (lumex::dev::genesis_key.pub.to_account ());
	auto dev_prv_text (lumex::dev::genesis_key.prv.to_string ());
	ASSERT_EQ (lumex::dev::genesis_key.pub, lumex::dev::genesis_key.pub);
}

TEST (block_store, empty_accounts)
{
	auto store = lumex::test::make_store ();
	auto transaction (store->tx_begin_read ());
	auto begin (store->account.begin (transaction));
	auto end (store->account.end (transaction));
	ASSERT_EQ (end, begin);
}

TEST (block_store, one_block)
{
	auto store = lumex::test::make_store ();

	lumex::block_builder builder;
	auto block1 = builder
				  .open ()
				  .source (0)
				  .representative (1)
				  .account (0)
				  .sign (lumex::keypair ().prv, 0)
				  .work (0)
				  .build ();
	block1->sideband_set ({});
	auto transaction (store->tx_begin_write ());
	store->block.put (transaction, block1->hash (), *block1);
	ASSERT_TRUE (store->block.exists (transaction, block1->hash ()));
}

TEST (block_store, empty_bootstrap)
{
	lumex::test::system system{};
	lumex::logger logger;
	unsigned max_unchecked_blocks = 65536;
	lumex::unchecked_map unchecked{ max_unchecked_blocks, system.stats, false };
	size_t count = 0;
	unchecked.for_each ([&count] (lumex::unchecked_key const & key, lumex::unchecked_info const & info) {
		++count;
	});
	ASSERT_EQ (count, 0);
}

TEST (block_store, unchecked_begin_search)
{
	auto store = lumex::test::make_store ();

	lumex::keypair key0;
	lumex::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (0)
				  .destination (1)
				  .balance (2)
				  .sign (key0.prv, key0.pub)
				  .work (3)
				  .build ();
	auto block2 = builder
				  .send ()
				  .previous (5)
				  .destination (6)
				  .balance (7)
				  .sign (key0.prv, key0.pub)
				  .work (8)
				  .build ();
}

TEST (block_store, frontier_retrieval)
{
	auto store = lumex::test::make_store ();

	lumex::account account1{};
	lumex::account_info info1 (0, 0, 0, 0, 0, 0, lumex::epoch::epoch_0);
	auto transaction (store->tx_begin_write ());
	store->account.put (transaction, account1, info1);
	lumex::account_info info2;
	store->account.get (transaction, account1, info2);
	ASSERT_EQ (info1, info2);
}

TEST (block_store, one_account)
{
	auto store = lumex::test::make_store ();

	lumex::account account{};
	lumex::block_hash hash (0);
	auto transaction (store->tx_begin_write ());
	store->confirmation_height.put (transaction, account, { 20, lumex::block_hash (15) });
	store->account.put (transaction, account, { hash, account, hash, 42, 100, 200, lumex::epoch::epoch_0 });
	auto begin (store->account.begin (transaction));
	auto end (store->account.end (transaction));
	ASSERT_NE (end, begin);
	ASSERT_EQ (account, lumex::account (begin->first));
	lumex::account_info info (begin->second);
	ASSERT_EQ (hash, info.head);
	ASSERT_EQ (42, info.balance.number ());
	ASSERT_EQ (100, info.modified);
	ASSERT_EQ (200, info.block_count);
	lumex::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store->confirmation_height.get (transaction, account, confirmation_height_info));
	ASSERT_EQ (20, confirmation_height_info.height);
	ASSERT_EQ (lumex::block_hash (15), confirmation_height_info.frontier);
	++begin;
	ASSERT_EQ (end, begin);
}

TEST (block_store, two_block)
{
	auto store = lumex::test::make_store ();

	lumex::block_builder builder;
	auto block1 = builder
				  .open ()
				  .source (0)
				  .representative (1)
				  .account (1)
				  .sign (lumex::keypair ().prv, 0)
				  .work (0)
				  .build ();
	block1->sideband_set ({});
	block1->hashables.account = 1;
	std::vector<lumex::block_hash> hashes;
	std::vector<lumex::open_block> blocks;
	hashes.push_back (block1->hash ());
	blocks.push_back (*block1);
	auto transaction (store->tx_begin_write ());
	store->block.put (transaction, hashes[0], *block1);
	auto block2 = builder
				  .open ()
				  .source (0)
				  .representative (1)
				  .account (2)
				  .sign (lumex::keypair ().prv, 0)
				  .work (0)
				  .build ();
	block2->sideband_set ({});
	hashes.push_back (block2->hash ());
	blocks.push_back (*block2);
	store->block.put (transaction, hashes[1], *block2);
	ASSERT_TRUE (store->block.exists (transaction, block1->hash ()));
	ASSERT_TRUE (store->block.exists (transaction, block2->hash ()));
}

TEST (block_store, two_account)
{
	auto store = lumex::test::make_store ();

	lumex::account account1 (1);
	lumex::block_hash hash1 (2);
	lumex::account account2 (3);
	lumex::block_hash hash2 (4);
	auto transaction (store->tx_begin_write ());
	store->confirmation_height.put (transaction, account1, { 20, lumex::block_hash (10) });
	store->account.put (transaction, account1, { hash1, account1, hash1, 42, 100, 300, lumex::epoch::epoch_0 });
	store->confirmation_height.put (transaction, account2, { 30, lumex::block_hash (20) });
	store->account.put (transaction, account2, { hash2, account2, hash2, 84, 200, 400, lumex::epoch::epoch_0 });
	auto begin (store->account.begin (transaction));
	auto end (store->account.end (transaction));
	ASSERT_NE (end, begin);
	ASSERT_EQ (account1, lumex::account (begin->first));
	lumex::account_info info1 (begin->second);
	ASSERT_EQ (hash1, info1.head);
	ASSERT_EQ (42, info1.balance.number ());
	ASSERT_EQ (100, info1.modified);
	ASSERT_EQ (300, info1.block_count);
	lumex::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store->confirmation_height.get (transaction, account1, confirmation_height_info));
	ASSERT_EQ (20, confirmation_height_info.height);
	ASSERT_EQ (lumex::block_hash (10), confirmation_height_info.frontier);
	++begin;
	ASSERT_NE (end, begin);
	ASSERT_EQ (account2, lumex::account (begin->first));
	lumex::account_info info2 (begin->second);
	ASSERT_EQ (hash2, info2.head);
	ASSERT_EQ (84, info2.balance.number ());
	ASSERT_EQ (200, info2.modified);
	ASSERT_EQ (400, info2.block_count);
	ASSERT_FALSE (store->confirmation_height.get (transaction, account2, confirmation_height_info));
	ASSERT_EQ (30, confirmation_height_info.height);
	ASSERT_EQ (lumex::block_hash (20), confirmation_height_info.frontier);
	++begin;
	ASSERT_EQ (end, begin);
}

TEST (block_store, latest_find)
{
	auto store = lumex::test::make_store ();

	lumex::account account1 (1);
	lumex::block_hash hash1 (2);
	lumex::account account2 (3);
	lumex::block_hash hash2 (4);
	auto transaction (store->tx_begin_write ());
	auto first (store->account.begin (transaction));
	auto second (store->account.begin (transaction));
	++second;
	auto find1 (store->account.begin (transaction, 1));
	ASSERT_EQ (first, find1);
	auto find2 (store->account.begin (transaction, 3));
	ASSERT_EQ (second, find2);
	auto find3 (store->account.begin (transaction, 2));
	ASSERT_EQ (second, find3);
}

// File can be shared
TEST (block_store, DISABLED_already_open)
{
	auto path (lumex::unique_path ());
	std::filesystem::create_directories (path.parent_path ());
	lumex::set_secure_perm_directory (path.parent_path ());
	std::ofstream file;
	file.open (path.string ().c_str ());
	ASSERT_TRUE (file.is_open ());
	auto store = lumex::test::make_store (path);
}

TEST (block_store, roots)
{
	auto store = lumex::test::make_store ();

	lumex::block_builder builder;
	auto send_block = builder
					  .send ()
					  .previous (0)
					  .destination (1)
					  .balance (2)
					  .sign (lumex::keypair ().prv, 4)
					  .work (5)
					  .build ();
	ASSERT_EQ (send_block->hashables.previous, send_block->root ().as_block_hash ());
	auto change_block = builder
						.change ()
						.previous (0)
						.representative (1)
						.sign (lumex::keypair ().prv, 3)
						.work (4)
						.build ();
	ASSERT_EQ (change_block->hashables.previous, change_block->root ().as_block_hash ());
	auto receive_block = builder
						 .receive ()
						 .previous (0)
						 .source (1)
						 .sign (lumex::keypair ().prv, 3)
						 .work (4)
						 .build ();
	ASSERT_EQ (receive_block->hashables.previous, receive_block->root ().as_block_hash ());
	auto open_block = builder
					  .open ()
					  .source (0)
					  .representative (1)
					  .account (2)
					  .sign (lumex::keypair ().prv, 4)
					  .work (5)
					  .build ();
	ASSERT_EQ (open_block->hashables.account, open_block->root ().as_account ());
}

TEST (block_store, pending_exists)
{
	auto store = lumex::test::make_store ();

	lumex::pending_key two (2, 0);
	lumex::pending_info pending;
	auto transaction (store->tx_begin_write ());
	store->pending.put (transaction, two, pending);
	lumex::pending_key one (1, 0);
	ASSERT_FALSE (store->pending.exists (transaction, one));
}

TEST (block_store, latest_exists)
{
	auto store = lumex::test::make_store ();

	lumex::account two (2);
	lumex::account_info info;
	auto transaction (store->tx_begin_write ());
	store->account.put (transaction, two, info);
	lumex::account one (1);
	ASSERT_FALSE (store->account.exists (transaction, one));
}

TEST (block_store, large_iteration)
{
	auto store = lumex::test::make_store ();

	std::unordered_set<lumex::account> accounts1;
	for (auto i (0); i < 1000; ++i)
	{
		auto transaction (store->tx_begin_write ());
		lumex::account account;
		lumex::random_pool::generate_block (account.bytes.data (), account.bytes.size ());
		accounts1.insert (account);
		store->account.put (transaction, account, lumex::account_info ());
	}
	std::unordered_set<lumex::account> accounts2;
	lumex::account previous{};
	auto transaction (store->tx_begin_read ());
	for (auto i (store->account.begin (transaction, 0)), n (store->account.end (transaction)); i != n; ++i)
	{
		lumex::account current (i->first);
		ASSERT_GT (current.number (), previous.number ());
		accounts2.insert (current);
		previous = current;
	}
	ASSERT_EQ (accounts1, accounts2);
	// Reverse iteration
	std::unordered_set<lumex::account> accounts3;
	previous = std::numeric_limits<lumex::uint256_t>::max ();
	for (auto i (store->account.rbegin (transaction)), n (store->account.rend (transaction)); i != n; ++i)
	{
		lumex::account current (i->first);
		ASSERT_LT (current.number (), previous.number ());
		accounts3.insert (current);
		previous = current;
	}
	ASSERT_EQ (accounts1, accounts3);
}

TEST (block_store, frontier)
{
	auto store = lumex::test::make_store ();
	auto transaction (store->tx_begin_write ());
	lumex::block_hash hash (100);
	lumex::account account (200);
}

TEST (block_store, block_replace)
{
	auto store = lumex::test::make_store ();

	lumex::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (0)
				 .destination (0)
				 .balance (0)
				 .sign (lumex::keypair ().prv, 0)
				 .work (1)
				 .build ();
	send1->sideband_set ({});
	auto send2 = builder
				 .send ()
				 .previous (0)
				 .destination (0)
				 .balance (0)
				 .sign (lumex::keypair ().prv, 0)
				 .work (2)
				 .build ();
	send2->sideband_set ({});
	auto transaction (store->tx_begin_write ());
	store->block.put (transaction, 0, *send1);
	store->block.put (transaction, 0, *send2);
	auto block3 (store->block.get (transaction, 0));
	ASSERT_NE (nullptr, block3);
	ASSERT_EQ (2, block3->block_work ());
}

TEST (block_store, block_count)
{
	auto store = lumex::test::make_store ();
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (0, store->block.count (transaction));
		lumex::block_builder builder;
		auto block = builder
					 .open ()
					 .source (0)
					 .representative (1)
					 .account (0)
					 .sign (lumex::keypair ().prv, 0)
					 .work (0)
					 .build ();
		block->sideband_set ({});
		auto hash1 (block->hash ());
		store->block.put (transaction, hash1, *block);
	}
	auto transaction (store->tx_begin_read ());
	ASSERT_EQ (1, store->block.count (transaction));
}

TEST (block_store, account_count)
{
	auto store = lumex::test::make_store ();
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (0, store->account.count (transaction));
		lumex::account account (200);
		store->account.put (transaction, account, lumex::account_info ());
	}
	auto transaction (store->tx_begin_read ());
	ASSERT_EQ (1, store->account.count (transaction));
}

TEST (block_store, cemented_count_cache)
{
	lumex::logger logger;
	lumex::stats stats{ logger };
	auto store = lumex::test::make_store (logger, stats);
	lumex::ledger ledger (*store, lumex::dev::network_params, stats, logger);
	ASSERT_EQ (1, ledger.cemented_count ());
}

TEST (block_store, pruned_random)
{
	auto store = lumex::test::make_store ();

	lumex::block_builder builder;
	auto block = builder
				 .open ()
				 .source (0)
				 .representative (1)
				 .account (0)
				 .sign (lumex::keypair ().prv, 0)
				 .work (0)
				 .build ();
	block->sideband_set ({});
	auto hash1 (block->hash ());
	{
		auto transaction (store->tx_begin_write ());
		lumex::ledger::seed_genesis (*store, transaction, lumex::dev::constants);
		store->pruned.put (transaction, hash1);
	}
	auto transaction (store->tx_begin_read ());
	auto random_hash (store->pruned.random (transaction));
	ASSERT_EQ (hash1, random_hash);
}

TEST (block_store, state_block)
{
	auto store = lumex::test::make_store ();

	lumex::keypair key1;
	lumex::block_builder builder;
	auto block1 = builder
				  .state ()
				  .account (1)
				  .previous (lumex::dev::genesis->hash ())
				  .representative (3)
				  .balance (4)
				  .link (6)
				  .sign (key1.prv, key1.pub)
				  .work (7)
				  .build ();

	block1->sideband_set ({});
	{
		auto transaction (store->tx_begin_write ());
		lumex::ledger::seed_genesis (*store, transaction, lumex::dev::constants);
		ASSERT_EQ (lumex::block_type::state, block1->type ());
		store->block.put (transaction, block1->hash (), *block1);
		ASSERT_TRUE (store->block.exists (transaction, block1->hash ()));
		auto block2 (store->block.get (transaction, block1->hash ()));
		ASSERT_NE (nullptr, block2);
		ASSERT_EQ (*block1, *block2);
	}
	{
		auto transaction (store->tx_begin_write ());
		auto count (store->block.count (transaction));
		ASSERT_EQ (2, count);
		store->block.del (transaction, block1->hash ());
		ASSERT_FALSE (store->block.exists (transaction, block1->hash ()));
	}
	auto transaction (store->tx_begin_read ());
	auto count2 (store->block.count (transaction));
	ASSERT_EQ (1, count2);
}

TEST (block_store, sideband_height)
{
	lumex::logger logger;
	lumex::stats stats{ logger };
	auto store = lumex::test::make_store (logger, stats);

	lumex::keypair key1;
	lumex::keypair key2;
	lumex::keypair key3;
	lumex::ledger ledger (*store, lumex::dev::network_params, stats, logger);
	lumex::block_builder builder;
	auto transaction = ledger.tx_begin_write ();
	lumex::work_pool pool{ lumex::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	auto send = builder
				.send ()
				.previous (lumex::dev::genesis->hash ())
				.destination (lumex::dev::genesis_key.pub)
				.balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*pool.generate (lumex::dev::genesis->hash ()))
				.build ();
	ASSERT_EQ (lumex::block_status::progress, ledger.process (transaction, send));
	auto receive = builder
				   .receive ()
				   .previous (send->hash ())
				   .source (send->hash ())
				   .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				   .work (*pool.generate (send->hash ()))
				   .build ();
	ASSERT_EQ (lumex::block_status::progress, ledger.process (transaction, receive));
	auto change = builder
				  .change ()
				  .previous (receive->hash ())
				  .representative (0)
				  .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				  .work (*pool.generate (receive->hash ()))
				  .build ();
	ASSERT_EQ (lumex::block_status::progress, ledger.process (transaction, change));
	auto state_send1 = builder
					   .state ()
					   .account (lumex::dev::genesis_key.pub)
					   .previous (change->hash ())
					   .representative (0)
					   .balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
					   .link (key1.pub)
					   .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					   .work (*pool.generate (change->hash ()))
					   .build ();
	ASSERT_EQ (lumex::block_status::progress, ledger.process (transaction, state_send1));
	auto state_send2 = builder
					   .state ()
					   .account (lumex::dev::genesis_key.pub)
					   .previous (state_send1->hash ())
					   .representative (0)
					   .balance (lumex::dev::constants.genesis_amount - 2 * lumex::Klumex_ratio)
					   .link (key2.pub)
					   .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					   .work (*pool.generate (state_send1->hash ()))
					   .build ();
	ASSERT_EQ (lumex::block_status::progress, ledger.process (transaction, state_send2));
	auto state_send3 = builder
					   .state ()
					   .account (lumex::dev::genesis_key.pub)
					   .previous (state_send2->hash ())
					   .representative (0)
					   .balance (lumex::dev::constants.genesis_amount - 3 * lumex::Klumex_ratio)
					   .link (key3.pub)
					   .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					   .work (*pool.generate (state_send2->hash ()))
					   .build ();
	ASSERT_EQ (lumex::block_status::progress, ledger.process (transaction, state_send3));
	auto state_open = builder
					  .state ()
					  .account (key1.pub)
					  .previous (0)
					  .representative (0)
					  .balance (lumex::Klumex_ratio)
					  .link (state_send1->hash ())
					  .sign (key1.prv, key1.pub)
					  .work (*pool.generate (key1.pub))
					  .build ();
	ASSERT_EQ (lumex::block_status::progress, ledger.process (transaction, state_open));
	auto epoch = builder
				 .state ()
				 .account (key1.pub)
				 .previous (state_open->hash ())
				 .representative (0)
				 .balance (lumex::Klumex_ratio)
				 .link (ledger.epoch_link (lumex::epoch::epoch_1))
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*pool.generate (state_open->hash ()))
				 .build ();
	ASSERT_EQ (lumex::block_status::progress, ledger.process (transaction, epoch));
	ASSERT_EQ (lumex::epoch::epoch_1, ledger.version (*epoch));
	auto epoch_open = builder
					  .state ()
					  .account (key2.pub)
					  .previous (0)
					  .representative (0)
					  .balance (0)
					  .link (ledger.epoch_link (lumex::epoch::epoch_1))
					  .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					  .work (*pool.generate (key2.pub))
					  .build ();
	ASSERT_EQ (lumex::block_status::progress, ledger.process (transaction, epoch_open));
	ASSERT_EQ (lumex::epoch::epoch_1, ledger.version (*epoch_open));
	auto state_receive = builder
						 .state ()
						 .account (key2.pub)
						 .previous (epoch_open->hash ())
						 .representative (0)
						 .balance (lumex::Klumex_ratio)
						 .link (state_send2->hash ())
						 .sign (key2.prv, key2.pub)
						 .work (*pool.generate (epoch_open->hash ()))
						 .build ();
	ASSERT_EQ (lumex::block_status::progress, ledger.process (transaction, state_receive));
	auto open = builder
				.open ()
				.source (state_send3->hash ())
				.representative (lumex::dev::genesis_key.pub)
				.account (key3.pub)
				.sign (key3.prv, key3.pub)
				.work (*pool.generate (key3.pub))
				.build ();
	ASSERT_EQ (lumex::block_status::progress, ledger.process (transaction, open));
	auto block1 = ledger.any.block_get (transaction, lumex::dev::genesis->hash ());
	ASSERT_EQ (block1->sideband ().height, 1);
	auto block2 = ledger.any.block_get (transaction, send->hash ());
	ASSERT_EQ (block2->sideband ().height, 2);
	auto block3 = ledger.any.block_get (transaction, receive->hash ());
	ASSERT_EQ (block3->sideband ().height, 3);
	auto block4 = ledger.any.block_get (transaction, change->hash ());
	ASSERT_EQ (block4->sideband ().height, 4);
	auto block5 = ledger.any.block_get (transaction, state_send1->hash ());
	ASSERT_EQ (block5->sideband ().height, 5);
	auto block6 = ledger.any.block_get (transaction, state_send2->hash ());
	ASSERT_EQ (block6->sideband ().height, 6);
	auto block7 = ledger.any.block_get (transaction, state_send3->hash ());
	ASSERT_EQ (block7->sideband ().height, 7);
	auto block8 = ledger.any.block_get (transaction, state_open->hash ());
	ASSERT_EQ (block8->sideband ().height, 1);
	auto block9 = ledger.any.block_get (transaction, epoch->hash ());
	ASSERT_EQ (block9->sideband ().height, 2);
	auto block10 = ledger.any.block_get (transaction, epoch_open->hash ());
	ASSERT_EQ (block10->sideband ().height, 1);
	auto block11 = ledger.any.block_get (transaction, state_receive->hash ());
	ASSERT_EQ (block11->sideband ().height, 2);
	auto block12 = ledger.any.block_get (transaction, open->hash ());
	ASSERT_EQ (block12->sideband ().height, 1);
}

TEST (block_store, peers)
{
	auto store = lumex::test::make_store ();

	lumex::endpoint_key endpoint (boost::asio::ip::address_v6::any ().to_bytes (), 100);
	{
		auto transaction (store->tx_begin_write ());

		// Confirm that the store is empty
		ASSERT_FALSE (store->peer.exists (transaction, endpoint));
		ASSERT_EQ (store->peer.count (transaction), 0);

		// Add one
		store->peer.put (transaction, endpoint, 37);
		ASSERT_TRUE (store->peer.exists (transaction, endpoint));
	}

	// Confirm that it can be found
	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer.count (transaction), 1);
		ASSERT_EQ (store->peer.get (transaction, endpoint), 37);
	}

	// Add another one and check that it (and the existing one) can be found
	lumex::endpoint_key endpoint1 (boost::asio::ip::address_v6::any ().to_bytes (), 101);
	{
		auto transaction (store->tx_begin_write ());
		store->peer.put (transaction, endpoint1, 42);
		ASSERT_TRUE (store->peer.exists (transaction, endpoint1)); // Check new peer is here
		ASSERT_TRUE (store->peer.exists (transaction, endpoint)); // Check first peer is still here
	}

	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer.count (transaction), 2);
		ASSERT_EQ (store->peer.get (transaction, endpoint), 37);
		ASSERT_EQ (store->peer.get (transaction, endpoint1), 42);
	}

	// Delete the first one
	{
		auto transaction (store->tx_begin_write ());
		store->peer.del (transaction, endpoint1);
		ASSERT_FALSE (store->peer.exists (transaction, endpoint1)); // Confirm it no longer exists
		ASSERT_TRUE (store->peer.exists (transaction, endpoint)); // Check first peer is still here
	}

	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer.count (transaction), 1);
	}

	// Delete original one
	{
		auto transaction (store->tx_begin_write ());
		store->peer.del (transaction, endpoint);
		ASSERT_FALSE (store->peer.exists (transaction, endpoint));
	}

	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer.count (transaction), 0);
	}
}

TEST (block_store, endpoint_key_byte_order)
{
	boost::asio::ip::address_v6 address (boost::asio::ip::make_address_v6 ("::ffff:127.0.0.1"));
	uint16_t port = 100;
	lumex::endpoint_key endpoint_key (address.to_bytes (), port);

	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream (bytes);
		lumex::write (stream, endpoint_key);
	}

	// This checks that the endpoint is serialized as expected, with a size
	// of 18 bytes (16 for ipv6 address and 2 for port), both in network byte order.
	ASSERT_EQ (bytes.size (), 18);
	ASSERT_EQ (bytes[10], 0xff);
	ASSERT_EQ (bytes[11], 0xff);
	ASSERT_EQ (bytes[12], 127);
	ASSERT_EQ (bytes[bytes.size () - 2], 0);
	ASSERT_EQ (bytes.back (), 100);

	// Deserialize the same stream bytes
	lumex::bufferstream stream1 (bytes.data (), bytes.size ());
	lumex::endpoint_key endpoint_key1;
	lumex::read (stream1, endpoint_key1);

	// This should be in network bytes order
	ASSERT_EQ (address.to_bytes (), endpoint_key1.address_bytes ());

	// This should be in host byte order
	ASSERT_EQ (port, endpoint_key1.port ());
}

TEST (block_store, online_weight)
{
	auto store = lumex::test::make_store ();
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (0, store->online_weight.count (transaction));
		ASSERT_EQ (store->online_weight.end (transaction), store->online_weight.begin (transaction));
		ASSERT_EQ (store->online_weight.rend (transaction), store->online_weight.rbegin (transaction));
		store->online_weight.put (transaction, 1, 2);
		store->online_weight.put (transaction, 3, 4);
	}
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (2, store->online_weight.count (transaction));
		auto item (store->online_weight.begin (transaction));
		ASSERT_NE (store->online_weight.end (transaction), item);
		ASSERT_EQ (1, item->first);
		ASSERT_EQ (2, item->second.number ());
		auto item_last (store->online_weight.rbegin (transaction));
		ASSERT_NE (store->online_weight.rend (transaction), item_last);
		ASSERT_EQ (3, item_last->first);
		ASSERT_EQ (4, item_last->second.number ());
		store->online_weight.del (transaction, 1);
		ASSERT_EQ (1, store->online_weight.count (transaction));
		ASSERT_EQ (*store->online_weight.begin (transaction), *store->online_weight.rbegin (transaction));
		store->online_weight.del (transaction, 3);
	}
	auto transaction (store->tx_begin_read ());
	ASSERT_EQ (0, store->online_weight.count (transaction));
	ASSERT_EQ (store->online_weight.end (transaction), store->online_weight.begin (transaction));
	ASSERT_EQ (store->online_weight.rend (transaction), store->online_weight.rbegin (transaction));
}

TEST (block_store, pruned_blocks)
{
	auto store = lumex::test::make_store ();

	lumex::keypair key1;
	lumex::block_builder builder;
	auto block1 = builder
				  .open ()
				  .source (0)
				  .representative (1)
				  .account (key1.pub)
				  .sign (key1.prv, key1.pub)
				  .work (0)
				  .build ();
	auto hash1 (block1->hash ());
	{
		auto transaction (store->tx_begin_write ());

		// Confirm that the store is empty
		ASSERT_FALSE (store->pruned.exists (transaction, hash1));
		ASSERT_EQ (store->pruned.count (transaction), 0);

		// Add one
		store->pruned.put (transaction, hash1);
		ASSERT_TRUE (store->pruned.exists (transaction, hash1));
	}

	// Confirm that it can be found
	ASSERT_EQ (store->pruned.count (store->tx_begin_read ()), 1);

	// Add another one and check that it (and the existing one) can be found
	auto block2 = builder
				  .open ()
				  .source (1)
				  .representative (2)
				  .account (key1.pub)
				  .sign (key1.prv, key1.pub)
				  .work (0)
				  .build ();
	block2->sideband_set ({});
	auto hash2 (block2->hash ());
	{
		auto transaction (store->tx_begin_write ());
		store->pruned.put (transaction, hash2);
		ASSERT_TRUE (store->pruned.exists (transaction, hash2)); // Check new pruned hash is here
		ASSERT_FALSE (store->block.exists (transaction, hash2));
		ASSERT_TRUE (store->pruned.exists (transaction, hash1)); // Check first pruned hash is still here
		ASSERT_FALSE (store->block.exists (transaction, hash1));
	}

	ASSERT_EQ (store->pruned.count (store->tx_begin_read ()), 2);

	// Delete the first one
	{
		auto transaction (store->tx_begin_write ());
		store->pruned.del (transaction, hash2);
		ASSERT_FALSE (store->pruned.exists (transaction, hash2)); // Confirm it no longer exists
		ASSERT_FALSE (store->block.exists (transaction, hash2)); // true for block_exists
		store->block.put (transaction, hash2, *block2); // Add corresponding block
		ASSERT_TRUE (store->block.exists (transaction, hash2));
		ASSERT_TRUE (store->pruned.exists (transaction, hash1)); // Check first pruned hash is still here
		ASSERT_FALSE (store->block.exists (transaction, hash1));
	}

	ASSERT_EQ (store->pruned.count (store->tx_begin_read ()), 1);

	// Delete original one
	{
		auto transaction (store->tx_begin_write ());
		store->pruned.del (transaction, hash1);
		ASSERT_FALSE (store->pruned.exists (transaction, hash1));
	}

	ASSERT_EQ (store->pruned.count (store->tx_begin_read ()), 0);
}

// Test various confirmation height values as well as clearing them
TEST (block_store, confirmation_height)
{
	auto path (lumex::unique_path ());
	auto store = lumex::test::make_store (path);

	lumex::account account1{};
	lumex::account account2{ 1 };
	lumex::account account3{ 2 };
	lumex::block_hash cemented_frontier1 (3);
	lumex::block_hash cemented_frontier2 (4);
	lumex::block_hash cemented_frontier3 (5);
	{
		auto transaction (store->tx_begin_write ());
		store->confirmation_height.put (transaction, account1, { 500, cemented_frontier1 });
		store->confirmation_height.put (transaction, account2, { std::numeric_limits<uint64_t>::max (), cemented_frontier2 });
		store->confirmation_height.put (transaction, account3, { 10, cemented_frontier3 });

		lumex::confirmation_height_info confirmation_height_info;
		ASSERT_FALSE (store->confirmation_height.get (transaction, account1, confirmation_height_info));
		ASSERT_EQ (confirmation_height_info.height, 500);
		ASSERT_EQ (confirmation_height_info.frontier, cemented_frontier1);
		ASSERT_FALSE (store->confirmation_height.get (transaction, account2, confirmation_height_info));
		ASSERT_EQ (confirmation_height_info.height, std::numeric_limits<uint64_t>::max ());
		ASSERT_EQ (confirmation_height_info.frontier, cemented_frontier2);
		ASSERT_FALSE (store->confirmation_height.get (transaction, account3, confirmation_height_info));
		ASSERT_EQ (confirmation_height_info.height, 10);
		ASSERT_EQ (confirmation_height_info.frontier, cemented_frontier3);
	}

	// Check clearing of confirmation heights
	store->confirmation_height.clear ();

	auto transaction (store->tx_begin_read ());
	ASSERT_TRUE (store->confirmation_height.empty (transaction));
	lumex::confirmation_height_info confirmation_height_info;
	ASSERT_TRUE (store->confirmation_height.get (transaction, account1, confirmation_height_info));
	ASSERT_TRUE (store->confirmation_height.get (transaction, account2, confirmation_height_info));
	ASSERT_TRUE (store->confirmation_height.get (transaction, account3, confirmation_height_info));
}

// Test various final vote values as well as clearing them
TEST (block_store, final_vote)
{
	auto path (lumex::unique_path ());
	auto store = lumex::test::make_store (path);

	auto qualified_root = lumex::dev::genesis->qualified_root ();
	{
		auto transaction (store->tx_begin_write ());
		store->final_vote.put (transaction, qualified_root, lumex::block_hash (2));
	}
	ASSERT_FALSE (store->final_vote.empty (store->tx_begin_read ()));
	store->final_vote.clear ();
	ASSERT_TRUE (store->final_vote.empty (store->tx_begin_read ()));
	{
		auto transaction (store->tx_begin_write ());
		store->final_vote.put (transaction, qualified_root, lumex::block_hash (2));
	}
	ASSERT_FALSE (store->final_vote.empty (store->tx_begin_read ()));
	{
		auto transaction (store->tx_begin_write ());
		// Clearing with correct root should remove
		store->final_vote.del (transaction, qualified_root);
	}
	ASSERT_TRUE (store->final_vote.empty (store->tx_begin_read ()));
}

TEST (block_store, topo_key_round_trip)
{
	lumex::topo_key key1{ 12345, lumex::block_hash{ 42 } };
	lumex::store::db_val val{ key1 };
	auto key2 = static_cast<lumex::topo_key> (val);
	ASSERT_EQ (key1.topo_height, key2.topo_height);
	ASSERT_EQ (key1.hash, key2.hash);
	ASSERT_EQ (key1, key2);
}

// Lexicographic byte order must match numeric order of topo_height (big-endian encoding),
// otherwise DB-level forward iteration would not yield blocks in topological order.
TEST (block_store, topo_key_ordering)
{
	lumex::topo_key a{ 1, lumex::block_hash{ 100 } };
	lumex::topo_key b{ 1, lumex::block_hash{ 200 } };
	lumex::topo_key c{ 2, lumex::block_hash{ 50 } };
	ASSERT_LT (a, b);
	ASSERT_LT (a, c);
	ASSERT_LT (b, c);

	lumex::store::db_val a_val{ a };
	lumex::store::db_val b_val{ b };
	lumex::store::db_val c_val{ c };
	auto bytes = [] (lumex::store::db_val const & v) {
		return std::span<uint8_t const>{ static_cast<uint8_t const *> (v.data ()), v.size () };
	};
	ASSERT_TRUE (std::lexicographical_compare (bytes (a_val).begin (), bytes (a_val).end (), bytes (b_val).begin (), bytes (b_val).end ()));
	ASSERT_TRUE (std::lexicographical_compare (bytes (a_val).begin (), bytes (a_val).end (), bytes (c_val).begin (), bytes (c_val).end ()));
	ASSERT_TRUE (std::lexicographical_compare (bytes (b_val).begin (), bytes (b_val).end (), bytes (c_val).begin (), bytes (c_val).end ()));
}

TEST (block_store, topology_view_put_exists_del)
{
	auto store = lumex::test::make_store ();
	lumex::block_hash hash_a{ 100 };
	lumex::block_hash hash_b{ 200 };

	{
		auto txn = store->tx_begin_read ();
		ASSERT_FALSE (store->topology.exists (txn, { 1, hash_a }));
		ASSERT_EQ (0, store->topology.count (txn));
	}
	{
		auto txn = store->tx_begin_write ();
		store->topology.put (txn, { 1, hash_a });
		store->topology.put (txn, { 2, hash_b });
	}
	{
		auto txn = store->tx_begin_read ();
		ASSERT_TRUE (store->topology.exists (txn, { 1, hash_a }));
		ASSERT_TRUE (store->topology.exists (txn, { 2, hash_b }));
		ASSERT_FALSE (store->topology.exists (txn, { 1, hash_b }));
		ASSERT_FALSE (store->topology.exists (txn, { 2, hash_a }));
		ASSERT_EQ (2, store->topology.count (txn));
	}
	{
		auto txn = store->tx_begin_write ();
		store->topology.del (txn, { 1, hash_a });
	}
	{
		auto txn = store->tx_begin_read ();
		ASSERT_FALSE (store->topology.exists (txn, { 1, hash_a }));
		ASSERT_TRUE (store->topology.exists (txn, { 2, hash_b }));
		ASSERT_EQ (1, store->topology.count (txn));
	}
}

// Deleting a non-existing key is a silent no-op
TEST (block_store, topology_view_del_nonexisting)
{
	auto store = lumex::test::make_store ();
	lumex::block_hash hash_a{ 100 };
	lumex::block_hash hash_b{ 200 };

	// Empty table: del must not assert
	{
		auto txn = store->tx_begin_write ();
		store->topology.del (txn, { 1, hash_a });
	}
	{
		auto txn = store->tx_begin_read ();
		ASSERT_EQ (0, store->topology.count (txn));
	}

	// Populated table: del of an unrelated key leaves existing rows untouched
	{
		auto txn = store->tx_begin_write ();
		store->topology.put (txn, { 1, hash_a });
		store->topology.del (txn, { 1, hash_b }); // same height, different hash
		store->topology.del (txn, { 2, hash_a }); // different height, same hash
		store->topology.del (txn, { 99, lumex::block_hash{ 999 } }); // unrelated key
	}
	{
		auto txn = store->tx_begin_read ();
		ASSERT_TRUE (store->topology.exists (txn, { 1, hash_a }));
		ASSERT_EQ (1, store->topology.count (txn));
	}

	// Deleting the same key twice is also a no-op on the second call
	{
		auto txn = store->tx_begin_write ();
		store->topology.del (txn, { 1, hash_a });
		store->topology.del (txn, { 1, hash_a });
	}
	{
		auto txn = store->tx_begin_read ();
		ASSERT_FALSE (store->topology.exists (txn, { 1, hash_a }));
		ASSERT_EQ (0, store->topology.count (txn));
	}
}

// Forward iteration must yield entries ordered by (topo_height, hash)
TEST (block_store, topology_view_iteration_ordered_by_topo_height)
{
	auto store = lumex::test::make_store ();
	std::vector<lumex::topo_key> inserted;
	inserted.emplace_back (3, lumex::block_hash{ 50 });
	inserted.emplace_back (1, lumex::block_hash{ 200 });
	inserted.emplace_back (2, lumex::block_hash{ 30 });
	inserted.emplace_back (1, lumex::block_hash{ 100 });
	inserted.emplace_back (2, lumex::block_hash{ 500 });

	{
		auto txn = store->tx_begin_write ();
		for (auto const & key : inserted)
		{
			store->topology.put (txn, key);
		}
	}

	auto txn = store->tx_begin_read ();
	std::vector<lumex::topo_key> seen;
	for (auto i = store->topology.begin (txn), end = store->topology.end (txn); i != end; ++i)
	{
		seen.push_back (i->first);
	}
	ASSERT_EQ (inserted.size (), seen.size ());
	ASSERT_TRUE (std::is_sorted (seen.begin (), seen.end ()));

	// `latest()` returns the highest topo_height (last entry by sort order).
	ASSERT_EQ (3, store->topology.latest (txn));

	// Seeking to an exact (topo_height, hash) yields that entry first.
	auto seek = store->topology.begin (txn, lumex::topo_key{ 2, lumex::block_hash{ 30 } });
	ASSERT_EQ (lumex::topo_key (2, lumex::block_hash{ 30 }), seek->first);

	// Seeking to just `topo_height` lands on the first hash at that height.
	auto seek_height = store->topology.begin (txn, 2);
	ASSERT_EQ (2u, seek_height->first.topo_height);
	ASSERT_EQ (lumex::block_hash{ 30 }, seek_height->first.hash);
}

TEST (block_store, topology_view_clear)
{
	auto store = lumex::test::make_store ();

	{
		auto txn = store->tx_begin_write ();
		store->topology.put (txn, { 1, lumex::block_hash{ 1 } });
		store->topology.put (txn, { 2, lumex::block_hash{ 2 } });
		store->topology.put (txn, { 3, lumex::block_hash{ 3 } });
	}
	store->topology.clear ();
	{
		auto txn = store->tx_begin_read ();
		ASSERT_EQ (0, store->topology.count (txn));
		ASSERT_FALSE (store->topology.latest (txn).has_value ());
	}
}

TEST (block_store, topology_view_latest_empty)
{
	auto store = lumex::test::make_store ();
	auto txn = store->tx_begin_read ();
	ASSERT_FALSE (store->topology.latest (txn).has_value ());
}

TEST (block_store, reset_renew_existing_transaction)
{
	auto store = lumex::test::make_store ();

	lumex::keypair key1;
	lumex::block_builder builder;
	auto block = builder
				 .open ()
				 .source (0)
				 .representative (1)
				 .account (1)
				 .sign (lumex::keypair ().prv, 0)
				 .work (0)
				 .build ();
	block->sideband_set ({});
	auto hash1 (block->hash ());
	auto read_transaction = store->tx_begin_read ();

	// Block shouldn't exist yet
	auto block_non_existing (store->block.get (read_transaction, hash1));
	ASSERT_EQ (nullptr, block_non_existing);

	// Release resources for the transaction
	read_transaction.reset ();

	// Write the block
	{
		auto write_transaction (store->tx_begin_write ());
		store->block.put (write_transaction, hash1, *block);
	}

	read_transaction.renew ();

	// Block should exist now
	auto block_existing (store->block.get (read_transaction, hash1));
	ASSERT_NE (nullptr, block_existing);
}

TEST (block_store, default_database_backend)
{
	auto backend = lumex::default_database_backend ();

	auto store = lumex::test::make_store ();

	auto vendor = store->get_vendor ();
	if (backend == lumex::database_backend::rocksdb)
	{
		ASSERT_TRUE (vendor.find ("RocksDB") != std::string::npos);
	}
	else
	{
		ASSERT_TRUE (vendor.find ("LMDB") != std::string::npos);
	}
}

TEST (block_store, lmdb_bad_path)
{
	if (lumex::default_database_backend () != lumex::database_backend::lmdb)
	{
		GTEST_SKIP ();
	}

	auto path = lumex::unique_path ();
	std::filesystem::create_directories (path);
	auto db_path = path / "data.ldb";
	// Create a dummy file and make it inaccessible
	{
		std::ofstream stream (db_path.c_str ());
	}
	std::filesystem::permissions (db_path, std::filesystem::perms::none);
	ASSERT_THROW (lumex::test::make_store (path), std::runtime_error);
	std::filesystem::permissions (db_path, std::filesystem::perms::all); // Cleanup
}

TEST (block_store, rocksdb_bad_path)
{
	if (lumex::default_database_backend () != lumex::database_backend::rocksdb)
	{
		GTEST_SKIP ();
	}

	auto path = lumex::unique_path ();
	std::filesystem::create_directories (path);
	auto db_path = path / "rocksdb";
	// Create a file where RocksDB expects a directory
	{
		std::ofstream stream (db_path.c_str ());
	}
	ASSERT_THROW (lumex::test::make_store (path), std::runtime_error);
}

// This test ensures the tombstone_count is increased when there is a delete.
// The tombstone_count is part of a flush logic bound to the way RocksDB is used by the node.
TEST (block_store, rocksdb_tombstone_count)
{
	if (lumex::default_database_backend () != lumex::database_backend::rocksdb)
	{
		GTEST_SKIP ();
	}

	lumex::logger logger;
	lumex::stats stats{ logger };
	auto path = lumex::unique_path () / "rocksdb";
	lumex::rocksdb_config config{};

	// Create backend directly and keep pointer for tombstone access
	auto backend = std::make_unique<lumex::store::rocksdb::backend_rocksdb> (path, config, logger);
	auto * rocksdb_backend = backend.get ();

	lumex::store::ledger_store store (std::move (backend), lumex::store::open_mode::read_write, stats, logger);

	auto tx = store.tx_begin_write ();
	lumex::ledger::seed_genesis (store, tx, lumex::dev::constants);

	lumex::account account{ 1 };
	store.account.put (tx, account, lumex::account_info{});

	// Verify account exists
	ASSERT_TRUE (store.account.exists (tx, account));
	ASSERT_EQ (rocksdb_backend->get_tombstone_map ().at (lumex::store::table::accounts).num_since_last_flush.load (), 0);

	// Perform delete and check tombstone counter
	store.account.del (tx, account);
	ASSERT_EQ (rocksdb_backend->get_tombstone_map ().at (lumex::store::table::accounts).num_since_last_flush.load (), 1);
}

TEST (block_store, meta_flags)
{
	auto store = lumex::test::make_store ();
	auto const key = static_cast<lumex::store::meta_key> (99);
	{
		auto txn = store->tx_begin_read ();
		ASSERT_FALSE (store->version.get_flag (txn, key));
	}
	{
		auto txn = store->tx_begin_write ();
		store->version.put_flag (txn, key, true);
	}
	{
		auto txn = store->tx_begin_read ();
		ASSERT_TRUE (store->version.get_flag (txn, key));
	}
	{
		auto txn = store->tx_begin_write ();
		store->version.put_flag (txn, key, false);
	}
	{
		auto txn = store->tx_begin_read ();
		ASSERT_FALSE (store->version.get_flag (txn, key));
	}
}