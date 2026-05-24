#include <lumex/lib/block_sideband.hpp>
#include <lumex/lib/block_type.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/config.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/stream.hpp>
#include <lumex/store/backend.hpp>
#include <lumex/store/block_w_sideband.hpp>
#include <lumex/store/crawler.hpp>
#include <lumex/store/db_val_templ.hpp>
#include <lumex/store/ledger/rep_weight.hpp>
#include <lumex/store/ledger/version.hpp>
#include <lumex/store/ledger_store.hpp>
#include <lumex/store/typed_iterator.hpp>
#include <lumex/store/typed_iterator_templ.hpp>

#include <boost/multiprecision/cpp_int.hpp>

namespace lumex::store
{
lumex::store::column_schema const ledger_store::schema_v21{
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

lumex::store::column_schema const ledger_store::schema_v22{
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

lumex::store::column_schema const ledger_store::schema_v23{
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

lumex::store::column_schema const ledger_store::schema_v24{
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

lumex::store::column_schema const ledger_store::schema_v25{
	{ lumex::store::table::blocks, "blocks" },
	{ lumex::store::table::accounts, "accounts" },
	{ lumex::store::table::pending, "pending" },
	{ lumex::store::table::rep_weights, "rep_weights" },
	{ lumex::store::table::online_weight, "online_weight" },
	{ lumex::store::table::pruned, "pruned" },
	{ lumex::store::table::successor, "successor" },
	{ lumex::store::table::peers, "peers" },
	{ lumex::store::table::confirmation_height, "confirmation_height" },
	{ lumex::store::table::final_votes, "final_votes" },
	{ lumex::store::table::meta, "meta" }
};

// Drop unchecked table
void ledger_store::upgrade_v21_to_v22 ()
{
	logger.info (lumex::log::type::ledger_upgrade, "Upgrading database from v21 to v22...");

	backend.open (schema_v21, lumex::store::open_mode::read_write);
	{
		release_assert (backend.get_version (backend.tx_begin_read ()) == 21, "unexpected version during upgrade", std::to_string (backend.get_version (backend.tx_begin_read ())));

		bool dropped = backend.drop_table ("unchecked");
		release_assert (dropped, "failed to drop unchecked table during upgrade");

		auto transaction = backend.tx_begin_write ();
		backend.set_version (transaction, 22);
	}
	backend.close ();

	logger.info (lumex::log::type::ledger_upgrade, "Upgrading database from v21 to v22 completed");
}

// Fill rep_weights table with all existing representatives and their vote weight
void ledger_store::upgrade_v22_to_v23 ()
{
	logger.info (lumex::log::type::ledger_upgrade, "Upgrading database from v22 to v23...");

	// Open with schema_v23 so we can access rep_weights table
	// This allows us to clear it if a previous upgrade attempt failed halfway
	backend.open (schema_v23, lumex::store::open_mode::read_write);
	{
		release_assert (backend.get_version (backend.tx_begin_read ()) == 22, "unexpected version during upgrade", std::to_string (backend.get_version (backend.tx_begin_read ())));

		// Always clear rep_weights table entries to ensure it's empty before populating
		// This can happen if an upgrade was attempted but failed halfway through
		auto clear_status = backend.clear (lumex::store::table::rep_weights);
		release_assert (backend.success (clear_status), "failed to clear rep_weights table during upgrade", backend.error_string (clear_status));

		auto transaction = backend.tx_begin_write ();

		release_assert (rep_weight.begin (backend.tx_begin_read ()) == rep_weight.end (transaction), "rep weights table must be empty before upgrading to v23");

		auto iterate_accounts = [this] (auto && func) {
			auto transaction = backend.tx_begin_read ();

			// Manually create v22 compatible iterator to read accounts
			auto it = lumex::store::typed_iterator<lumex::account, lumex::account_info_v22>{ backend.begin (transaction, lumex::store::table::accounts) };
			auto const end = lumex::store::typed_iterator<lumex::account, lumex::account_info_v22>{ backend.end (transaction, lumex::store::table::accounts) };

			for (; it != end; ++it)
			{
				auto const & account = it->first;
				auto const & account_info = it->second;

				func (account, account_info);
			}
		};

		// Smaller batch size for dev runs to potentially trigger edge cases
		const size_t batch_size = lumex::is_dev_run () ? 2 : 250000;

		size_t processed = 0;
		iterate_accounts ([this, &transaction, &processed, batch_size] (lumex::account const & account, lumex::account_info_v22 const & account_info) {
			if (!account_info.balance.is_zero ())
			{
				lumex::uint128_t total{ 0 };
				lumex::store::db_val value;
				auto status = backend.get (transaction, lumex::store::table::rep_weights, account_info.representative, value);
				if (backend.success (status))
				{
					total = lumex::amount{ value }.number ();
				}
				total += account_info.balance.number ();
				status = backend.put (transaction, lumex::store::table::rep_weights, account_info.representative, lumex::amount{ total });
				backend.release_assert_success (status);
			}

			processed++;
			if (processed % batch_size == 0)
			{
				logger.info (lumex::log::type::ledger_upgrade, "Processed {} accounts", processed);
				transaction.refresh (); // Refresh to prevent excessive memory usage
			}
		});

		logger.info (lumex::log::type::ledger_upgrade, "Done processing {} accounts", processed);
		version.put_version (transaction, 23);
	}
	backend.close ();

	logger.info (lumex::log::type::ledger_upgrade, "Upgrading database from v22 to v23 completed");
}

// Drop frontiers table
void ledger_store::upgrade_v23_to_v24 ()
{
	logger.info (lumex::log::type::ledger_upgrade, "Upgrading database from v23 to v24...");

	backend.open (schema_v23, lumex::store::open_mode::read_write);
	{
		release_assert (backend.get_version (backend.tx_begin_read ()) == 23, "unexpected version during upgrade", std::to_string (backend.get_version (backend.tx_begin_read ())));

		bool dropped = backend.drop_table ("frontiers");
		release_assert (dropped, "failed to drop frontiers table during upgrade");

		auto transaction = backend.tx_begin_write ();
		version.put_version (transaction, 24);
	}
	backend.close ();

	logger.info (lumex::log::type::ledger_upgrade, "Upgrading database from v23 to v24 completed");
}

// Populate dedicated successor table from block sideband data
void ledger_store::upgrade_v24_to_v25 ()
{
	logger.info (lumex::log::type::ledger_upgrade, "Upgrading database from v24 to v25...");

	// Open with schema_v25 so we have access to the successor table
	backend.open (schema_v25, lumex::store::open_mode::read_write);
	{
		release_assert (backend.get_version (backend.tx_begin_read ()) == 24, "unexpected version during upgrade", std::to_string (backend.get_version (backend.tx_begin_read ())));

		// Always clear successor table to ensure clean state before populating
		auto clear_result = backend.clear (lumex::store::table::successor);
		release_assert (backend.success (clear_result), "failed to clear successor table during upgrade", backend.error_string (clear_result));

		auto transaction = backend.tx_begin_write ();

		// Smaller batch size for dev runs to potentially trigger edge cases
		const size_t batch_size = lumex::is_dev_run () ? 2 : 250000;
		size_t processed = 0;
		auto const total_blocks = backend.count (backend.tx_begin_read (), lumex::store::table::blocks);

		// Iterate all blocks using a separate read transaction
		// Use block_w_sideband_v25 to read old format with successor in sideband
		auto iterate_blocks = [this] (auto && func) {
			auto read_txn = backend.tx_begin_read ();
			auto it = lumex::store::typed_iterator<lumex::block_hash, lumex::store::block_w_sideband_v25>{ backend.begin (read_txn, lumex::store::table::blocks) };
			auto const end = lumex::store::typed_iterator<lumex::block_hash, lumex::store::block_w_sideband_v25>{ backend.end (read_txn, lumex::store::table::blocks) };
			for (; it != end; ++it)
			{
				auto const & [hash, block_w_sideband] = *it;
				func (hash, block_w_sideband);
			}
		};

		iterate_blocks ([this, &transaction, &processed, batch_size, total_blocks] (lumex::block_hash const & hash, lumex::store::block_w_sideband_v25 const & block_w_sideband) {
			// If successor is non-zero, write to successor table
			if (!block_w_sideband.sideband.successor.is_zero ())
			{
				auto status = backend.put (transaction, lumex::store::table::successor, hash, block_w_sideband.sideband.successor);
				backend.release_assert_success (status);
			}

			processed++;
			if (processed % batch_size == 0)
			{
				logger.info (lumex::log::type::ledger_upgrade, "Processed {} blocks ({:.1f}%)", processed, lumex::log::percentage (processed, total_blocks));
				transaction.refresh ();
			}
		});

		logger.info (lumex::log::type::ledger_upgrade, "Done processing {} blocks", processed);
		version.put_version (transaction, 25);
	}
	backend.close ();

	logger.info (lumex::log::type::ledger_upgrade, "Upgrading database from v24 to v25 completed");
}

// Remove successor from block sideband and add topo_height placeholder sideband field
void ledger_store::upgrade_v25_to_v26 ()
{
	logger.info (lumex::log::type::ledger_upgrade, "Upgrading database from v25 to v26...");

	// Open with schema_v25
	backend.open (schema_v25, lumex::store::open_mode::read_write);
	{
		release_assert (backend.get_version (backend.tx_begin_read ()) == 25, "unexpected version during upgrade", std::to_string (backend.get_version (backend.tx_begin_read ())));

		auto transaction = backend.tx_begin_write ();

		// Smaller batch size for dev runs to potentially trigger edge cases
		size_t const batch_size = lumex::is_dev_run () ? 2 : 250000;

		auto const total_blocks = backend.count (backend.tx_begin_read (), lumex::store::table::blocks);

		size_t processed = 0;
		size_t skipped = 0;

		// View for raw block table iteration (preserves raw bytes for mixed v25/v26 format detection)
		struct raw_blocks_view
		{
			using iterator = lumex::store::typed_iterator<lumex::block_hash, lumex::store::db_val>;

			lumex::store::backend & backend;

			iterator begin (lumex::store::transaction const & txn, lumex::block_hash const & key) const
			{
				return iterator{ backend.begin (txn, lumex::store::table::blocks, lumex::store::db_val{ key }) };
			}
			iterator end (lumex::store::transaction const & txn) const
			{
				return iterator{ backend.end (txn, lumex::store::table::blocks) };
			}
		} raw_view{ backend };

		lumex::store::crawler crawler{ raw_view, transaction };

		size_t batch_count = 0;
		for (; crawler; ++crawler)
		{
			auto const & [hash, raw_val] = *crawler;

			// Deserialize the block to determine its type and detect sideband format by remaining size
			lumex::bufferstream stream{ raw_val.span_view.data (), raw_val.span_view.size () };
			auto block = lumex::deserialize_block (stream);
			release_assert (block, "failed to deserialize block during v25->v26 upgrade");

			auto const remaining = static_cast<size_t> (stream.in_avail ());
			auto const expected_v25_size = lumex::block_sideband_v25::size (block->type ());
			auto const expected_v26_size = lumex::block_sideband::size (block->type ());

			if (remaining == expected_v26_size)
			{
				// Block already in v26 format (from a previous interrupted upgrade), skip
				skipped++;
			}
			else
			{
				release_assert (remaining == expected_v25_size, "unexpected sideband size during v25->v26 upgrade", std::to_string (remaining));

				// Deserialize v25 sideband
				lumex::block_sideband_v25 sideband_v25;
				auto error = sideband_v25.deserialize (stream, block->type ());
				release_assert (!error, "failed to deserialize v25 sideband during upgrade");

				// Rewrite block with new sideband format (no successor)
				std::vector<uint8_t> data;
				{
					lumex::vectorstream out_stream{ data };
					lumex::serialize_block (out_stream, *block);
					lumex::block_sideband current_sideband{
						sideband_v25.account,
						sideband_v25.balance,
						sideband_v25.height,
						sideband_v25.timestamp,
						sideband_v25.details,
						sideband_v25.source_epoch
					};
					current_sideband.serialize (out_stream, block->type ());
				}

				lumex::store::db_val value{ data.size (), data.data () };
				auto status = backend.put (transaction, lumex::store::table::blocks, hash, value);
				backend.release_assert_success (status);

				processed++;
			}

			batch_count++;
			if (batch_count >= batch_size)
			{
				logger.info (lumex::log::type::ledger_upgrade, "Processed {} blocks ({:.1f}%)", processed + skipped, lumex::log::percentage (processed + skipped, total_blocks));

				// Refresh transaction to commit changes
				crawler.refresh ();

				batch_count = 0;
			}
		}

		crawler.refresh ();

		logger.info (lumex::log::type::ledger_upgrade, "Done processing {} blocks ({} converted, {} already upgraded)", processed + skipped, processed, skipped);
		version.put_version (transaction, 26);
	}

	backend.close ();

	logger.info (lumex::log::type::ledger_upgrade, "Upgrading database from v25 to v26 completed");
}
}
