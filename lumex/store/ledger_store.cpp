#include <lumex/lib/block_sideband.hpp>
#include <lumex/lib/block_type.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/config.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/stream.hpp>
#include <lumex/secure/network_params.hpp>
#include <lumex/store/backend.hpp>
#include <lumex/store/db_val_templ.hpp>
#include <lumex/store/ledger/account.hpp>
#include <lumex/store/ledger/block.hpp>
#include <lumex/store/ledger/confirmation_height.hpp>
#include <lumex/store/ledger/final_vote.hpp>
#include <lumex/store/ledger/online_weight.hpp>
#include <lumex/store/ledger/peer.hpp>
#include <lumex/store/ledger/pending.hpp>
#include <lumex/store/ledger/pruned.hpp>
#include <lumex/store/ledger/rep_weight.hpp>
#include <lumex/store/ledger/successor.hpp>
#include <lumex/store/ledger/topology.hpp>
#include <lumex/store/ledger/version.hpp>
#include <lumex/store/ledger_store.hpp>

namespace lumex::store
{
lumex::store::column_schema const ledger_store::schema_current{
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
	{ lumex::store::table::topology, "topology" },
	{ lumex::store::table::meta, "meta" }
};
}

namespace lumex::store
{
ledger_store::ledger_store (std::unique_ptr<lumex::store::backend> backend_a, lumex::store::open_mode mode, lumex::stats & stats_a, lumex::logger & logger_a, ledger_store_params params) :
	stats{ stats_a },
	logger{ logger_a },
	backend_impl{ std::move (backend_a) },
	successor_impl{ std::make_unique<lumex::store::ledger::successor_view> (*backend_impl) },
	block_impl{ std::make_unique<lumex::store::ledger::block_view> (*backend_impl, *successor_impl) },
	account_impl{ std::make_unique<lumex::store::ledger::account_view> (*backend_impl) },
	pending_impl{ std::make_unique<lumex::store::ledger::pending_view> (*backend_impl) },
	rep_weight_impl{ std::make_unique<lumex::store::ledger::rep_weight_view> (*backend_impl) },
	online_weight_impl{ std::make_unique<lumex::store::ledger::online_weight_view> (*backend_impl) },
	pruned_impl{ std::make_unique<lumex::store::ledger::pruned_view> (*backend_impl) },
	peer_impl{ std::make_unique<lumex::store::ledger::peer_view> (*backend_impl) },
	confirmation_height_impl{ std::make_unique<lumex::store::ledger::confirmation_height_view> (*backend_impl) },
	final_vote_impl{ std::make_unique<lumex::store::ledger::final_vote_view> (*backend_impl) },
	topology_impl{ std::make_unique<lumex::store::ledger::topology_view> (*backend_impl) },
	version_impl{ std::make_unique<lumex::store::ledger::version_view> (*backend_impl) },
	backend{ *backend_impl },
	successor{ *successor_impl },
	block{ *block_impl },
	account{ *account_impl },
	pending{ *pending_impl },
	rep_weight{ *rep_weight_impl },
	online_weight{ *online_weight_impl },
	pruned{ *pruned_impl },
	peer{ *peer_impl },
	confirmation_height{ *confirmation_height_impl },
	final_vote{ *final_vote_impl },
	topology{ *topology_impl },
	version{ *version_impl }
{
	// Skip automatic open/upgrade when defer_open is set (used for testing individual upgrades)
	if (params.defer_open)
	{
		return;
	}

	logger.info (lumex::log::type::ledger_store, "Initializing ledger store: {}", backend.get_database_path ());

	bool needs_upgrade = false;
	bool fresh_db = false;
	backend_meta meta{};

	if (auto meta_opt = backend.fetch_meta ())
	{
		meta = *meta_opt;

		logger.debug (lumex::log::type::ledger_store, "Ledger database version: {}", meta.version);

		// Prevent opening future database versions
		if (meta.version > version_current)
		{
			logger.error (lumex::log::type::ledger_store, "The version of the ledger database ({}) is higher than the current ({}) which is supported. Either upgrade your node software or use a different database.", meta.version, version_current);

			throw std::runtime_error ("Ledger version " + std::to_string (meta.version) + " is higher than current version " + std::to_string (version_current));
		}

		// Minimum supported upgrade version check
		if (meta.version < version_minimum)
		{
			logger.error (lumex::log::type::ledger_store, "The version of the ledger database ({}) is lower than the minimum ({}) which is supported for upgrades. Perform an intermediate upgrade with an older node version or perform a fresh bootstrap.", meta.version, version_minimum);

			throw std::runtime_error ("Ledger version " + std::to_string (meta.version) + " is lower than minimum supported version " + std::to_string (version_minimum));
		}

		// Check if upgrade is needed
		if (meta.version < version_current)
		{
			needs_upgrade = true;

			logger.info (lumex::log::type::ledger_store, "The ledger database needs to be upgraded from version {} to {}", meta.version, version_current);
		}
	}
	else
	{
		fresh_db = true;

		logger.info (lumex::log::type::ledger_store, "No existing ledger found, a new database will be created.");
	}
	release_assert (meta.version > 0 || fresh_db);

	if (needs_upgrade || fresh_db)
	{
		if (mode == lumex::store::open_mode::read_only)
		{
			throw std::runtime_error ("Database requires upgrade but was opened in read-only mode");
		}
	}

	if (needs_upgrade)
	{
		if (params.backup_before_upgrade)
		{
			logger.info (lumex::log::type::ledger_store, "Creating ledger backup before upgrade...");

			backend.open (backend::schema_meta, lumex::store::open_mode::read_only);
			backend.backup ();
			backend.close ();

			logger.info (lumex::log::type::ledger_store, "Ledger backup completed, continuing with upgrade...");
		}

		perform_upgrades (meta);
	}

	if (fresh_db)
	{
		logger.info (lumex::log::type::ledger_store, "Creating new ledger database with version {} at '{}'",
		version_current, backend.get_database_path ());

		backend.create (schema_current, version_current);
	}

	backend.open (schema_current, mode);

	release_assert (backend.get_meta ().version == version_current, "ledger database version after initialization is not current");
}

ledger_store::~ledger_store () = default;

bool ledger_store::empty (lumex::store::transaction const & txn) const
{
	for (auto const & [table, table_name] : schema_current)
	{
		if (table == lumex::store::table::meta)
		{
			continue; // Ignore meta table
		}
		if (backend.begin (txn, table) != backend.end (txn, table))
		{
			return false;
		}
		debug_assert (backend.count (txn, table) == 0);
	}
	return true;
}

void ledger_store::perform_upgrades (lumex::store::backend_meta meta)
{
	debug_assert (meta.version < version_current, "perform_upgrades called but no upgrade is necessary");
	release_assert (meta.version >= version_minimum, "perform_upgrades called but version is below minimum supported version", std::to_string (meta.version));

	switch (meta.version)
	{
		case 21:
			upgrade_v21_to_v22 ();
			[[fallthrough]];
		case 22:
			upgrade_v22_to_v23 ();
			[[fallthrough]];
		case 23:
			upgrade_v23_to_v24 ();
			[[fallthrough]];
		case 24:
			upgrade_v24_to_v25 ();
			[[fallthrough]];
		case 25:
			upgrade_v25_to_v26 ();
			[[fallthrough]];
		case 26:
			break;
		default:
			release_assert (false, "invalid ledger database version for upgrade", std::to_string (meta.version));
	}
}

uint64_t ledger_store::get_version () const
{
	return version.get_version (backend.tx_begin_read ());
}

std::string ledger_store::get_vendor () const
{
	return backend.get_vendor ();
}

std::filesystem::path ledger_store::get_database_path () const
{
	return backend.get_database_path ();
}

lumex::store::open_mode ledger_store::get_mode () const
{
	release_assert (backend.get_mode ().has_value (), "ledger_store::get_mode called but backend is not open");
	return backend.get_mode ().value ();
}

uint64_t ledger_store::count (lumex::store::transaction const & txn, lumex::store::table table) const
{
	return backend.count (txn, table);
}

lumex::store::write_transaction ledger_store::tx_begin_write ()
{
	return backend.tx_begin_write ();
}

lumex::store::read_transaction ledger_store::tx_begin_read () const
{
	return backend.tx_begin_read ();
}
}
