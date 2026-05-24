#include <lumex/lib/block_sideband.hpp>
#include <lumex/lib/block_type.hpp>
#include <lumex/lib/blockbuilders.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/files.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/node/make_store.hpp>
#include <lumex/secure/account_info.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/network_params.hpp>
#include <lumex/store/backend.hpp>
#include <lumex/store/db_val_templ.hpp>
#include <lumex/store/ledger/account.hpp>
#include <lumex/store/ledger/block.hpp>
#include <lumex/store/ledger/pending.hpp>
#include <lumex/store/ledger/rep_weight.hpp>
#include <lumex/store/ledger/successor.hpp>
#include <lumex/store/ledger/version.hpp>
#include <lumex/store/ledger_store.hpp>
#include <lumex/store/tables.hpp>
#include <lumex/store/typed_iterator.hpp>
#include <lumex/test_common/common.hpp>
#include <lumex/test_common/make_store.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <filesystem>

using namespace std::chrono_literals;

namespace
{
/*
 * Schema definitions matching historical ledger versions
 */
lumex::store::column_schema const schema_v21{
	{ lumex::store::table::blocks, "blocks" },
	{ lumex::store::table::accounts, "accounts" },
	{ lumex::store::table::pending, "pending" },
	{ lumex::store::table::online_weight, "online_weight" },
	{ lumex::store::table::pruned, "pruned" },
	{ lumex::store::table::peers, "peers" },
	{ lumex::store::table::confirmation_height, "confirmation_height" },
	{ lumex::store::table::final_votes, "final_votes" },
	{ lumex::store::table::frontiers, "frontiers" },
	{ lumex::store::table::unchecked, "unchecked" },
	{ lumex::store::table::meta, "meta" }
};

lumex::store::column_schema const schema_v22{
	{ lumex::store::table::blocks, "blocks" },
	{ lumex::store::table::accounts, "accounts" },
	{ lumex::store::table::pending, "pending" },
	{ lumex::store::table::online_weight, "online_weight" },
	{ lumex::store::table::pruned, "pruned" },
	{ lumex::store::table::peers, "peers" },
	{ lumex::store::table::confirmation_height, "confirmation_height" },
	{ lumex::store::table::final_votes, "final_votes" },
	{ lumex::store::table::frontiers, "frontiers" },
	{ lumex::store::table::meta, "meta" }
};

lumex::store::column_schema const schema_v23{
	{ lumex::store::table::blocks, "blocks" },
	{ lumex::store::table::accounts, "accounts" },
	{ lumex::store::table::pending, "pending" },
	{ lumex::store::table::rep_weights, "rep_weights" },
	{ lumex::store::table::online_weight, "online_weight" },
	{ lumex::store::table::pruned, "pruned" },
	{ lumex::store::table::peers, "peers" },
	{ lumex::store::table::confirmation_height, "confirmation_height" },
	{ lumex::store::table::final_votes, "final_votes" },
	{ lumex::store::table::frontiers, "frontiers" },
	{ lumex::store::table::meta, "meta" }
};

lumex::store::column_schema const schema_v24{
	{ lumex::store::table::blocks, "blocks" },
	{ lumex::store::table::accounts, "accounts" },
	{ lumex::store::table::pending, "pending" },
	{ lumex::store::table::rep_weights, "rep_weights" },
	{ lumex::store::table::online_weight, "online_weight" },
	{ lumex::store::table::pruned, "pruned" },
	{ lumex::store::table::peers, "peers" },
	{ lumex::store::table::confirmation_height, "confirmation_height" },
	{ lumex::store::table::final_votes, "final_votes" },
	{ lumex::store::table::meta, "meta" }
};

}

/*
 * Test that opening a database with a version higher than supported fails
 */
TEST (ledger_upgrades, version_too_high)
{
	// Create database with a future version
	auto const path = lumex::unique_path ();
	{
		auto backend = lumex::test::make_backend (path);
		backend->create (lumex::store::ledger_store::schema_current, 999);
	}

	// Attempting to open through ledger_store should throw
	ASSERT_THROW (
	lumex::store::ledger_store (
	lumex::test::make_backend (path),
	lumex::store::open_mode::read_write,
	lumex::test::default_stats (),
	lumex::test::default_logger ()),
	std::runtime_error);
}

/*
 * Test that opening a database with a version lower than minimum fails
 */
TEST (ledger_upgrades, version_too_low)
{
	// Create database with a version below minimum
	auto const path = lumex::unique_path ();
	{
		auto backend = lumex::test::make_backend (path);
		backend->create (lumex::store::ledger_store::schema_current, 7);
	}

	// Attempting to open through ledger_store should throw
	ASSERT_THROW (
	lumex::store::ledger_store (
	lumex::test::make_backend (path),
	lumex::store::open_mode::read_write,
	lumex::test::default_stats (),
	lumex::test::default_logger ()),
	std::runtime_error);
}

/*
 * Test that read-only mode prevents upgrades
 */
TEST (ledger_upgrades, read_only_prevents_upgrade)
{
	// Create a v21 database
	auto const path = lumex::unique_path ();
	{
		auto backend = lumex::test::make_backend (path);
		backend->create (schema_v21, 21);
	}

	// Attempting to open in read-only mode should fail because upgrade is needed
	ASSERT_THROW (
	lumex::store::ledger_store (
	lumex::test::make_backend (path),
	lumex::store::open_mode::read_only,
	lumex::test::default_stats (),
	lumex::test::default_logger ()),
	std::runtime_error);
}

/*
 * Test opening an initialized store in read-only mode
 */
TEST (ledger_upgrades, current_version_read_only)
{
	// Create and initialize a current version database
	auto const path = lumex::unique_path ();
	{
		lumex::store::ledger_store store (
		lumex::test::make_backend (path),
		lumex::store::open_mode::read_write,
		lumex::test::default_stats (),
		lumex::test::default_logger ());

		auto tx = store.tx_begin_write ();
		lumex::ledger::seed_genesis (store, tx, lumex::dev::constants);
	}

	// Open in read-only mode - should succeed since no upgrade needed
	lumex::store::ledger_store store (
	lumex::test::make_backend (path),
	lumex::store::open_mode::read_only,
	lumex::test::default_stats (),
	lumex::test::default_logger ());

	auto tx = store.tx_begin_read ();
	ASSERT_EQ (store.version.get_version (tx), lumex::store::ledger_store::version_current);

	// Verify we can read genesis account
	lumex::account_info info;
	ASSERT_FALSE (store.account.get (tx, lumex::dev::genesis_key.pub, info));
}

/*
 * Test that a current version database opens without upgrade
 */
TEST (ledger_upgrades, current_version_no_upgrade)
{
	// Create a current version database
	auto const path = lumex::unique_path ();
	{
		lumex::store::ledger_store store (
		lumex::test::make_backend (path),
		lumex::store::open_mode::read_write,
		lumex::test::default_stats (),
		lumex::test::default_logger ());

		auto tx = store.tx_begin_write ();
		lumex::ledger::seed_genesis (store, tx, lumex::dev::constants);
	}

	// Open again - should not require any upgrade
	lumex::store::ledger_store store (
	lumex::test::make_backend (path),
	lumex::store::open_mode::read_write,
	lumex::test::default_stats (),
	lumex::test::default_logger ());

	auto tx = store.tx_begin_read ();
	ASSERT_EQ (store.version.get_version (tx), lumex::store::ledger_store::version_current);
}

namespace
{
class legacy_database_v21
{
public:
	legacy_database_v21 (std::filesystem::path const & path_a) :
		path{ path_a },
		backend{ lumex::test::make_backend (path_a) }
	{
		backend->create (schema_v21, 21);
		backend->open (schema_v21, lumex::store::open_mode::read_write);
	}

	// Insert dummy data into unchecked table for testing upgrade that drops it
	void add_unchecked (uint64_t key_value, uint64_t data_value)
	{
		auto tx = backend->tx_begin_write ();
		lumex::store::db_val key{ sizeof (key_value), &key_value };
		lumex::store::db_val value{ sizeof (data_value), &data_value };
		auto status = backend->put (tx, lumex::store::table::unchecked, key, value);
		backend->release_assert_success (status);
	}

	void add_account (lumex::account const & account, lumex::account_info_v22 const & info)
	{
		auto tx = backend->tx_begin_write ();
		auto status = backend->put (tx, lumex::store::table::accounts, account, info);
		backend->release_assert_success (status);
	}

	std::filesystem::path path;
	std::unique_ptr<lumex::store::backend> backend;
};
}

/*
 * Test v21 to v22 upgrade: removes unchecked table
 */
TEST (ledger_upgrades, upgrade_v21_to_v22)
{
	// Create a v21 database with data in unchecked table
	auto const path = lumex::unique_path ();
	{
		legacy_database_v21 legacy_db{ path };
		legacy_db.add_unchecked (1, 100);
		legacy_db.add_unchecked (2, 200);

		// Verify unchecked table exists before upgrade
		ASSERT_TRUE (legacy_db.backend->table_exists ("unchecked"));
		auto tx = legacy_db.backend->tx_begin_read ();
		ASSERT_EQ (legacy_db.backend->count (tx, lumex::store::table::unchecked), 2);
	}

	// Perform the upgrade
	{
		lumex::store::ledger_store_params params;
		params.defer_open = true;

		lumex::store::ledger_store store (
		lumex::test::make_backend (path),
		lumex::store::open_mode::read_write,
		lumex::test::default_stats (),
		lumex::test::default_logger (),
		params);

		store.upgrade_v21_to_v22 ();
	}

	// Verify version is now 22 and unchecked table no longer exists
	{
		auto backend = lumex::test::make_backend (path);
		backend->open (schema_v22, lumex::store::open_mode::read_only);
		auto tx = backend->tx_begin_read ();
		ASSERT_EQ (backend->get_version (tx), 22);
		ASSERT_FALSE (backend->table_exists ("unchecked"));
	}
}

namespace
{
class legacy_database_v22
{
public:
	legacy_database_v22 (std::filesystem::path const & path_a) :
		path{ path_a },
		backend{ lumex::test::make_backend (path_a) }
	{
		backend->create (schema_v22, 22);
		backend->open (schema_v22, lumex::store::open_mode::read_write);
	}

	void add_account (lumex::account const & account, lumex::account_info_v22 const & info)
	{
		auto tx = backend->tx_begin_write ();
		auto status = backend->put (tx, lumex::store::table::accounts, account, info);
		backend->release_assert_success (status);
	}

	std::filesystem::path path;
	std::unique_ptr<lumex::store::backend> backend;
};
}

/*
 * Test v22 to v23 upgrade: populates rep_weights from account data
 */
TEST (ledger_upgrades, upgrade_v22_to_v23_rep_weights)
{
	lumex::account const account_1{ 1 };
	lumex::account const account_2{ 2 };
	lumex::account const account_3{ 3 };
	lumex::account const rep_a{ 41 };
	lumex::account const rep_b{ 43 };

	// Create a v22 database with test accounts
	auto const path = lumex::unique_path ();
	{
		legacy_database_v22 legacy_db{ path };

		// Account 1: balance 1000, rep_a
		lumex::account_info_v22 info1{};
		info1.representative = rep_a;
		info1.balance = 1000;
		legacy_db.add_account (account_1, info1);

		// Account 2: balance 500, rep_a (same rep as account 1)
		lumex::account_info_v22 info2{};
		info2.representative = rep_a;
		info2.balance = 500;
		legacy_db.add_account (account_2, info2);

		// Account 3: balance 42, rep_b
		lumex::account_info_v22 info3{};
		info3.representative = rep_b;
		info3.balance = 42;
		legacy_db.add_account (account_3, info3);
	}

	// Open through ledger_store which should trigger upgrade
	lumex::store::ledger_store store (
	lumex::test::make_backend (path),
	lumex::store::open_mode::read_write,
	lumex::test::default_stats (),
	lumex::test::default_logger ());

	// Verify rep weights were correctly calculated
	auto tx = store.tx_begin_read ();
	ASSERT_EQ (store.version.get_version (tx), lumex::store::ledger_store::version_current);

	// rep_a should have weight from account_1 + account_2 = 1000 + 500 = 1500
	ASSERT_EQ (store.rep_weight.get (tx, rep_a), 1500);

	// rep_b should have weight from account_3 = 42
	ASSERT_EQ (store.rep_weight.get (tx, rep_b), 42);
}

/*
 * Test v22 to v23 upgrade: zero balance accounts don't contribute to rep weight
 */
TEST (ledger_upgrades, upgrade_v22_to_v23_zero_balance)
{
	lumex::account const rep{ 100 };
	lumex::account const account_with_balance{ 1 };
	lumex::account const account_zero_balance{ 2 };

	// Create a v22 database with accounts (one with zero balance)
	auto const path = lumex::unique_path ();
	{
		legacy_database_v22 legacy_db{ path };

		lumex::account_info_v22 info1{};
		info1.representative = rep;
		info1.balance = 1000;
		legacy_db.add_account (account_with_balance, info1);

		lumex::account_info_v22 info2{};
		info2.representative = rep;
		info2.balance = 0; // Zero balance
		legacy_db.add_account (account_zero_balance, info2);
	}

	// Open through ledger_store which should trigger upgrade
	lumex::store::ledger_store store (
	lumex::test::make_backend (path),
	lumex::store::open_mode::read_write,
	lumex::test::default_stats (),
	lumex::test::default_logger ());

	// Verify rep weight only includes non-zero balance accounts
	auto tx = store.tx_begin_read ();
	ASSERT_EQ (store.rep_weight.get (tx, rep), 1000);
}

/*
 * Test v22 to v23 upgrade with many accounts to verify batch processing
 */
TEST (ledger_upgrades, upgrade_v22_to_v23_batch_processing)
{
	// Create multiple reps with multiple accounts
	std::vector<lumex::account> reps;
	std::map<lumex::account, lumex::uint128_t> expected_weights;

	for (int i = 0; i < 5; ++i)
	{
		reps.push_back (lumex::account{ static_cast<uint64_t> (1000 + i) });
		expected_weights[reps.back ()] = 0;
	}

	// Create a v22 database with many accounts
	auto const path = lumex::unique_path ();
	{
		legacy_database_v22 legacy_db{ path };

		for (int i = 0; i < 50; ++i)
		{
			lumex::account account{ static_cast<uint64_t> (i + 1) };
			auto & rep = reps[i % reps.size ()];
			lumex::uint128_t balance = (i + 1) * 100;

			lumex::account_info_v22 info{};
			info.representative = rep;
			info.balance = balance;
			legacy_db.add_account (account, info);

			expected_weights[rep] += balance;
		}
	}

	// Open through ledger_store which should trigger upgrade
	lumex::store::ledger_store store (
	lumex::test::make_backend (path),
	lumex::store::open_mode::read_write,
	lumex::test::default_stats (),
	lumex::test::default_logger ());

	// Verify rep weights were correctly calculated
	auto tx = store.tx_begin_read ();
	for (auto const & [rep, expected_weight] : expected_weights)
	{
		ASSERT_EQ (store.rep_weight.get (tx, rep), expected_weight)
		<< "Rep weight mismatch for rep " << rep.to_account ();
	}
}

/*
 * Test v22 to v23 upgrade: handles partially populated rep_weights from failed upgrade
 * This simulates a scenario where a previous upgrade attempt wrote some data to rep_weights but didn't complete.
 */
TEST (ledger_upgrades, upgrade_v22_to_v23_stale_rep_weights)
{
	lumex::account const rep_a{ 41 };
	lumex::account const rep_b{ 43 };
	lumex::account const rep_stale{ 99 }; // Stale entry from previous failed upgrade
	lumex::account const account_1{ 1 };
	lumex::account const account_2{ 2 };

	auto const path = lumex::unique_path ();
	{
		// Create v22 database with accounts
		legacy_database_v22 legacy_db{ path };

		lumex::account_info_v22 info1{};
		info1.representative = rep_a;
		info1.balance = 1000;
		legacy_db.add_account (account_1, info1);

		lumex::account_info_v22 info2{};
		info2.representative = rep_b;
		info2.balance = 500;
		legacy_db.add_account (account_2, info2);
	}

	// Simulate a failed upgrade that partially populated rep_weights
	{
		auto backend = lumex::test::make_backend (path);
		// Open with schema_v23 to create rep_weights table (but version stays at 22)
		backend->open (schema_v23, lumex::store::open_mode::read_write);
		{
			auto tx = backend->tx_begin_write ();

			// Add stale/incorrect data as if upgrade crashed mid-way
			// - rep_a has wrong value
			// - rep_stale shouldn't exist (was from deleted account in previous attempt)
			backend->put (tx, lumex::store::table::rep_weights, rep_a, lumex::amount{ 999999 }); // Wrong value
			backend->put (tx, lumex::store::table::rep_weights, rep_stale, lumex::amount{ 12345 }); // Stale entry

			// Version is still 22 (upgrade didn't complete)
			ASSERT_EQ (backend->get_version (tx), 22);
		}
		backend->close ();
	}

	// Now open through ledger_store which should properly handle the crash recovery
	lumex::store::ledger_store store (
	lumex::test::make_backend (path),
	lumex::store::open_mode::read_write,
	lumex::test::default_stats (),
	lumex::test::default_logger ());

	// Verify rep weights are correct (not corrupted by stale data)
	auto tx = store.tx_begin_read ();
	ASSERT_EQ (store.version.get_version (tx), lumex::store::ledger_store::version_current);

	// rep_a should have correct weight from account_1 = 1000 (not stale 999999)
	ASSERT_EQ (store.rep_weight.get (tx, rep_a), 1000);

	// rep_b should have correct weight from account_2 = 500
	ASSERT_EQ (store.rep_weight.get (tx, rep_b), 500);

	// rep_stale should not exist (it was cleared during proper upgrade)
	ASSERT_EQ (store.rep_weight.get (tx, rep_stale), 0);
}

namespace
{
class legacy_database_v23
{
public:
	legacy_database_v23 (std::filesystem::path const & path_a) :
		path{ path_a },
		backend{ lumex::test::make_backend (path_a) }
	{
		backend->create (schema_v23, 23);
		backend->open (schema_v23, lumex::store::open_mode::read_write);
	}

	// Insert dummy data into frontiers table for testing upgrade that drops it
	void add_frontier (uint64_t key_value, uint64_t data_value)
	{
		auto tx = backend->tx_begin_write ();
		lumex::store::db_val key{ sizeof (key_value), &key_value };
		lumex::store::db_val value{ sizeof (data_value), &data_value };
		auto status = backend->put (tx, lumex::store::table::frontiers, key, value);
		backend->release_assert_success (status);
	}

	std::filesystem::path path;
	std::unique_ptr<lumex::store::backend> backend;
};
}

/*
 * Test v23 to v24 upgrade: removes frontiers table
 */
TEST (ledger_upgrades, upgrade_v23_to_v24)
{
	// Create a v23 database with data in frontiers table
	auto const path = lumex::unique_path ();
	{
		legacy_database_v23 legacy_db{ path };
		legacy_db.add_frontier (1, 100);
		legacy_db.add_frontier (2, 200);

		// Verify frontiers table exists before upgrade
		ASSERT_TRUE (legacy_db.backend->table_exists ("frontiers"));
		auto tx = legacy_db.backend->tx_begin_read ();
		ASSERT_EQ (legacy_db.backend->count (tx, lumex::store::table::frontiers), 2);
	}

	// Perform the upgrade
	{
		lumex::store::ledger_store_params params;
		params.defer_open = true;

		lumex::store::ledger_store store (
		lumex::test::make_backend (path),
		lumex::store::open_mode::read_write,
		lumex::test::default_stats (),
		lumex::test::default_logger (),
		params);

		store.upgrade_v23_to_v24 ();
	}

	// Verify version is now 24 and frontiers table no longer exists
	{
		auto backend = lumex::test::make_backend (path);
		backend->open (schema_v24, lumex::store::open_mode::read_only);
		auto tx = backend->tx_begin_read ();
		ASSERT_EQ (backend->get_version (tx), 24);
		ASSERT_FALSE (backend->table_exists ("frontiers"));
	}
}

namespace
{
class legacy_database_v24
{
public:
	legacy_database_v24 (std::filesystem::path const & path_a) :
		path{ path_a },
		backend{ lumex::test::make_backend (path_a) }
	{
		backend->create (schema_v24, 24);
		backend->open (schema_v24, lumex::store::open_mode::read_write);
	}

	// Write a block with v24 sideband format (successor is part of sideband)
	void add_block (lumex::block & block, lumex::block_hash const & successor_hash)
	{
		auto tx = backend->tx_begin_write ();

		lumex::block_sideband_v25 sideband_v25;
		sideband_v25.successor = successor_hash;
		sideband_v25.account = block.sideband ().account;
		sideband_v25.balance = block.sideband ().balance;
		sideband_v25.height = block.sideband ().height;
		sideband_v25.timestamp = block.sideband ().timestamp;
		sideband_v25.details = block.sideband ().details;
		sideband_v25.source_epoch = block.sideband ().source_epoch;

		std::vector<uint8_t> data;
		{
			lumex::vectorstream stream{ data };
			lumex::serialize_block (stream, block);
			sideband_v25.serialize (stream, block.type ());
		}

		lumex::store::db_val value{ data.size (), data.data () };
		auto status = backend->put (tx, lumex::store::table::blocks, block.hash (), value);
		backend->release_assert_success (status);
	}

	std::filesystem::path path;
	std::unique_ptr<lumex::store::backend> backend;
};
}

/*
 * Test v24 to v25 upgrade: copies successor from block sideband to dedicated table
 */
TEST (ledger_upgrades, upgrade_v24_to_v25)
{
	lumex::keypair key1;
	lumex::keypair key2;

	lumex::block_builder builder;

	// Create an open block (genesis-like, no predecessor)
	auto block1 = builder
				  .open ()
				  .source (lumex::block_hash{ key1.pub.number () })
				  .representative (key1.pub)
				  .account (key1.pub)
				  .sign (key1.prv, key1.pub)
				  .work (0)
				  .build ();
	block1->sideband_set (lumex::block_sideband{
	key1.pub,
	lumex::amount{ 1000 },
	1, 0, lumex::epoch::epoch_0,
	false, false, false, lumex::epoch::epoch_0 });

	// Create a state block with a previous (block1)
	auto block2 = builder
				  .state ()
				  .account (key1.pub)
				  .previous (block1->hash ())
				  .representative (key1.pub)
				  .balance (500)
				  .link (key2.pub)
				  .sign (key1.prv, key1.pub)
				  .work (0)
				  .build ();
	block2->sideband_set (lumex::block_sideband{
	key1.pub,
	lumex::amount{ 500 },
	2, 0, lumex::epoch::epoch_0,
	true, false, false, lumex::epoch::epoch_0 });

	auto const path = lumex::unique_path ();
	{
		legacy_database_v24 legacy_db{ path };

		// block1 has block2 as successor
		legacy_db.add_block (*block1, block2->hash ());

		// block2 has no successor (zero hash)
		legacy_db.add_block (*block2, lumex::block_hash{ 0 });
	}

	// Open through ledger_store which should trigger upgrade
	lumex::store::ledger_store store (
	lumex::test::make_backend (path),
	lumex::store::open_mode::read_write,
	lumex::test::default_stats (),
	lumex::test::default_logger ());

	auto tx = store.tx_begin_read ();

	// Verify version
	ASSERT_EQ (store.version.get_version (tx), lumex::store::ledger_store::version_current);

	// Verify successor table has the correct entry for block1 -> block2
	auto successor_result = store.successor.get (tx, block1->hash ());
	ASSERT_TRUE (successor_result.has_value ());
	ASSERT_EQ (*successor_result, block2->hash ());

	// Verify block2 has no successor in the table
	auto no_successor = store.successor.get (tx, block2->hash ());
	ASSERT_FALSE (no_successor.has_value ());

	auto stored_block2 = store.block.get (tx, block2->hash ());
	ASSERT_NE (nullptr, stored_block2);
	ASSERT_EQ (stored_block2->sideband ().height, 2);
}

namespace
{
class legacy_database_v25
{
public:
	legacy_database_v25 (std::filesystem::path const & path_a) :
		path{ path_a },
		backend{ lumex::test::make_backend (path_a) }
	{
		backend->create (lumex::store::ledger_store::schema_v25, 25);
		backend->open (lumex::store::ledger_store::schema_v25, lumex::store::open_mode::read_write);
	}

	// Write a block with v25 sideband format (successor is part of sideband)
	void add_block_legacy (lumex::block & block, lumex::block_hash const & successor_hash)
	{
		auto tx = backend->tx_begin_write ();

		lumex::block_sideband_v25 sideband_v25;
		sideband_v25.successor = successor_hash;
		sideband_v25.account = block.sideband ().account;
		sideband_v25.balance = block.sideband ().balance;
		sideband_v25.height = block.sideband ().height;
		sideband_v25.timestamp = block.sideband ().timestamp;
		sideband_v25.details = block.sideband ().details;
		sideband_v25.source_epoch = block.sideband ().source_epoch;

		std::vector<uint8_t> data;
		{
			lumex::vectorstream stream{ data };
			lumex::serialize_block (stream, block);
			sideband_v25.serialize (stream, block.type ());
		}

		lumex::store::db_val value{ data.size (), data.data () };
		auto status = backend->put (tx, lumex::store::table::blocks, block.hash (), value);
		backend->release_assert_success (status);

		// Also populate successor table (as v25 migration would have done)
		if (!successor_hash.is_zero ())
		{
			status = backend->put (tx, lumex::store::table::successor, block.hash (), successor_hash);
			backend->release_assert_success (status);
		}
	}

	// Write a block with v26 sideband format (no successor in sideband)
	void add_block (lumex::block & block, lumex::block_hash const & successor_hash)
	{
		auto tx = backend->tx_begin_write ();

		std::vector<uint8_t> data;
		{
			lumex::vectorstream stream{ data };
			lumex::serialize_block (stream, block);
			block.sideband ().serialize (stream, block.type ());
		}

		lumex::store::db_val value{ data.size (), data.data () };
		auto status = backend->put (tx, lumex::store::table::blocks, block.hash (), value);
		backend->release_assert_success (status);

		if (!successor_hash.is_zero ())
		{
			status = backend->put (tx, lumex::store::table::successor, block.hash (), successor_hash);
			backend->release_assert_success (status);
		}
	}

	std::filesystem::path path;
	std::unique_ptr<lumex::store::backend> backend;
};
}

/*
 * Test v25 to v26 upgrade: removes successor from block sideband
 */
TEST (ledger_upgrades, upgrade_v25_to_v26)
{
	lumex::keypair key1;
	lumex::keypair key2;

	lumex::block_builder builder;

	// Create an open block (genesis-like, no predecessor)
	auto block1 = builder
				  .open ()
				  .source (lumex::block_hash{ key1.pub.number () })
				  .representative (key1.pub)
				  .account (key1.pub)
				  .sign (key1.prv, key1.pub)
				  .work (0)
				  .build ();
	block1->sideband_set (lumex::block_sideband{
	key1.pub,
	lumex::amount{ 1000 },
	1, 0, lumex::epoch::epoch_0,
	false, false, false, lumex::epoch::epoch_0 });

	// Create a state block with a previous (block1)
	auto block2 = builder
				  .state ()
				  .account (key1.pub)
				  .previous (block1->hash ())
				  .representative (key1.pub)
				  .balance (500)
				  .link (key2.pub)
				  .sign (key1.prv, key1.pub)
				  .work (0)
				  .build ();
	block2->sideband_set (lumex::block_sideband{
	key1.pub,
	lumex::amount{ 500 },
	2, 0, lumex::epoch::epoch_0,
	true, false, false, lumex::epoch::epoch_0 });

	// Record old sideband sizes (v25 format, includes successor)
	auto const old_size_open = lumex::block_sideband_v25::size (lumex::block_type::open);
	auto const old_size_state = lumex::block_sideband_v25::size (lumex::block_type::state);

	// Record new sideband sizes (v26 format, no successor)
	auto const new_size_open = lumex::block_sideband::size (lumex::block_type::open);
	auto const new_size_state = lumex::block_sideband::size (lumex::block_type::state);

	// Verify the new format is 24 bytes smaller (removed 32-byte successor, added 8-byte topo_index)
	ASSERT_EQ (old_size_open - new_size_open, 24);
	ASSERT_EQ (old_size_state - new_size_state, 24);

	auto const path = lumex::unique_path ();
	{
		legacy_database_v25 legacy_db{ path };

		// block1 has block2 as successor
		legacy_db.add_block_legacy (*block1, block2->hash ());

		// block2 has no successor (zero hash)
		legacy_db.add_block_legacy (*block2, lumex::block_hash{ 0 });

		// Verify blocks are stored with v25 format size
		auto tx = legacy_db.backend->tx_begin_read ();
		lumex::store::db_val value;
		auto status = legacy_db.backend->get (tx, lumex::store::table::blocks, block1->hash (), value);
		ASSERT_TRUE (legacy_db.backend->success (status));
	}

	// Open through ledger_store which should trigger upgrade
	lumex::store::ledger_store store (
	lumex::test::make_backend (path),
	lumex::store::open_mode::read_write,
	lumex::test::default_stats (),
	lumex::test::default_logger ());

	auto tx = store.tx_begin_read ();

	// Verify version is now 26
	ASSERT_EQ (store.version.get_version (tx), lumex::store::ledger_store::version_current);
	ASSERT_EQ (store.version.get_version (tx), 26);

	// Verify blocks are readable with new format
	auto stored_block1 = store.block.get (tx, block1->hash ());
	ASSERT_NE (nullptr, stored_block1);
	ASSERT_EQ (stored_block1->sideband ().height, 1);
	ASSERT_EQ (stored_block1->sideband ().balance, lumex::amount{ 1000 });

	auto stored_block2 = store.block.get (tx, block2->hash ());
	ASSERT_NE (nullptr, stored_block2);
	ASSERT_EQ (stored_block2->sideband ().height, 2);
	ASSERT_TRUE (stored_block2->sideband ().details.is_send);

	// Verify successor table still works
	auto successor_result = store.successor.get (tx, block1->hash ());
	ASSERT_TRUE (successor_result.has_value ());
	ASSERT_EQ (*successor_result, block2->hash ());

	auto no_successor = store.successor.get (tx, block2->hash ());
	ASSERT_FALSE (no_successor.has_value ());
}

/*
 * Test v25 to v26 upgrade resilience: handles interrupted upgrade where some blocks
 * are already in v26 format and some are still in v25 format
 */
TEST (ledger_upgrades, upgrade_v25_to_v26_interrupted)
{
	lumex::keypair key1;
	lumex::keypair key2;

	lumex::block_builder builder;

	auto block1 = builder
				  .open ()
				  .source (lumex::block_hash{ key1.pub.number () })
				  .representative (key1.pub)
				  .account (key1.pub)
				  .sign (key1.prv, key1.pub)
				  .work (0)
				  .build ();
	block1->sideband_set (lumex::block_sideband{
	key1.pub,
	lumex::amount{ 1000 },
	1, 0, lumex::epoch::epoch_0,
	false, false, false, lumex::epoch::epoch_0 });

	auto block2 = builder
				  .state ()
				  .account (key1.pub)
				  .previous (block1->hash ())
				  .representative (key1.pub)
				  .balance (500)
				  .link (key2.pub)
				  .sign (key1.prv, key1.pub)
				  .work (0)
				  .build ();
	block2->sideband_set (lumex::block_sideband{
	key1.pub,
	lumex::amount{ 500 },
	2, 0, lumex::epoch::epoch_0,
	true, false, false, lumex::epoch::epoch_0 });

	auto const path = lumex::unique_path ();
	{
		// Simulate an interrupted v25->v26 upgrade:
		// block1 is already in v26 format (no successor in sideband)
		// block2 is still in v25 format (successor in sideband)
		legacy_database_v25 legacy_db{ path };

		// block1 already converted to v26 format
		legacy_db.add_block (*block1, block2->hash ());

		// block2 still in v25 format (not yet converted)
		legacy_db.add_block_legacy (*block2, lumex::block_hash{ 0 });
	}

	// Open through ledger_store which should trigger v25->v26 upgrade
	// Must handle the mixed v25/v26 format blocks
	lumex::store::ledger_store store (
	lumex::test::make_backend (path),
	lumex::store::open_mode::read_write,
	lumex::test::default_stats (),
	lumex::test::default_logger ());

	auto tx = store.tx_begin_read ();

	ASSERT_EQ (store.version.get_version (tx), 26);

	// Both blocks should be readable
	auto stored_block1 = store.block.get (tx, block1->hash ());
	ASSERT_NE (nullptr, stored_block1);
	ASSERT_EQ (stored_block1->sideband ().height, 1);
	ASSERT_EQ (stored_block1->sideband ().balance, lumex::amount{ 1000 });

	auto stored_block2 = store.block.get (tx, block2->hash ());
	ASSERT_NE (nullptr, stored_block2);
	ASSERT_EQ (stored_block2->sideband ().height, 2);
	ASSERT_TRUE (stored_block2->sideband ().details.is_send);

	// Successor table should still work
	auto successor_result = store.successor.get (tx, block1->hash ());
	ASSERT_TRUE (successor_result.has_value ());
	ASSERT_EQ (*successor_result, block2->hash ());
}

/*
 * Test full upgrade path from v21 to current version
 */
TEST (ledger_upgrades, full_upgrade_v21_to_current)
{
	lumex::account const rep{ 100 };
	lumex::account const account{ 1 };

	// Create a v21 database with test data
	auto const path = lumex::unique_path ();
	{
		legacy_database_v21 legacy_db{ path };

		lumex::account_info_v22 info{};
		info.representative = rep;
		info.balance = 5000;
		legacy_db.add_account (account, info);
	}

	// Open through ledger_store which should trigger full upgrade chain
	lumex::store::ledger_store store (
	lumex::test::make_backend (path),
	lumex::store::open_mode::read_write,
	lumex::test::default_stats (),
	lumex::test::default_logger ());

	// Verify final version
	auto tx = store.tx_begin_read ();
	ASSERT_EQ (store.version.get_version (tx), lumex::store::ledger_store::version_current);

	// Verify account still exists
	lumex::account_info account_info;
	ASSERT_FALSE (store.account.get (tx, account, account_info));
	ASSERT_EQ (account_info.balance, 5000);

	// Verify rep_weight was populated during v22->v23 upgrade
	ASSERT_EQ (store.rep_weight.get (tx, rep), 5000);
}

/*
 * Test that backup is created before upgrade
 */
TEST (ledger_upgrades, upgrade_backup)
{
	auto const path = lumex::unique_path ();
	auto const is_rocksdb = lumex::default_database_backend () == lumex::database_backend::rocksdb;

	// Helper to check if backup exists
	auto backup_exists = [&] () {
		if (is_rocksdb)
		{
			// RocksDB creates backup/ directory
			return std::filesystem::exists (path / "backup");
		}
		else
		{
			// LMDB creates data_backup_<timestamp>.ldb file
			for (auto const & entry : std::filesystem::directory_iterator (path))
			{
				if (entry.path ().filename ().string ().find ("data_backup_") != std::string::npos)
				{
					return true;
				}
			}
			return false;
		}
	};

	// Create a v21 database (requires upgrade to current)
	{
		auto backend = lumex::test::make_backend (path);
		backend->create (schema_v21, 21);
	}

	// Verify no backup exists yet
	ASSERT_FALSE (backup_exists ());

	// Open with backup_before_upgrade=true, triggering upgrade and backup
	lumex::store::ledger_store_params params;
	params.backup_before_upgrade = true;

	lumex::store::ledger_store store (
	lumex::test::make_backend (path),
	lumex::store::open_mode::read_write,
	lumex::test::default_stats (),
	lumex::test::default_logger (),
	params);

	// Verify version was upgraded
	auto tx = store.tx_begin_read ();
	ASSERT_EQ (store.version.get_version (tx), lumex::store::ledger_store::version_current);

	// Verify backup was created
	ASSERT_TRUE (backup_exists ());
}
