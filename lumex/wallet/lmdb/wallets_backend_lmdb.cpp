#include <lumex/lib/logging.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/store/lmdb/common.hpp>
#include <lumex/store/lmdb/db_val.hpp>
#include <lumex/store/lmdb/iterator.hpp>
#include <lumex/store/lmdb/utility.hpp>
#include <lumex/wallet/lmdb/wallets_backend_lmdb.hpp>

#include <stdexcept>

namespace lumex::wallet::lmdb
{
wallets_backend_lmdb::wallets_backend_lmdb (std::filesystem::path const & path, lumex::lmdb_config const & config) :
	environment (path, lumex::store::lmdb::env::options::make ().set_config (config).override_config_sync (lumex::lmdb_config::sync_strategy::always).override_config_map_size (1ULL * 1024 * 1024 * 1024))
{
	auto wallet_txn = environment.tx_begin_write ();
	auto status1 = mdb_dbi_open (environment.tx (wallet_txn), nullptr, MDB_CREATE, &wallet_index_dbi);
	release_assert (lumex::store::lmdb::success (status1), lumex::store::lmdb::error_string (status1));
	auto status2 = mdb_dbi_open (environment.tx (wallet_txn), send_action_ids_name.data (), MDB_CREATE, &send_action_ids_dbi);
	release_assert (lumex::store::lmdb::success (status2), lumex::store::lmdb::error_string (status2));
}

lumex::store::read_transaction wallets_backend_lmdb::tx_begin_read () const
{
	return environment.tx_begin_read ();
}

lumex::store::write_transaction wallets_backend_lmdb::tx_begin_write ()
{
	return environment.tx_begin_write ();
}

wallet_handle wallets_backend_lmdb::wallet_open_or_create (lumex::store::write_transaction const & wallet_txn, std::string const & wallet_id_hex)
{
	MDB_dbi dbi{};
	auto status = mdb_dbi_open (environment.tx (wallet_txn), wallet_id_hex.c_str (), MDB_CREATE, &dbi);
	if (!lumex::store::lmdb::success (status))
	{
		throw std::runtime_error ("Failed to open wallet database '" + wallet_id_hex + "': " + lumex::store::lmdb::error_string (status));
	}
	return wallet_handle{ dbi };
}

void wallets_backend_lmdb::wallet_drop (lumex::store::write_transaction const & wallet_txn, wallet_handle & handle)
{
	if (!handle)
	{
		return;
	}
	auto status = mdb_drop (environment.tx (wallet_txn), handle_for (handle), /* delete from database */ 1);
	release_assert (lumex::store::lmdb::success (status), lumex::store::lmdb::error_string (status));
	handle = wallet_handle{};
}

std::optional<lumex::store::db_val> wallets_backend_lmdb::entry_get (lumex::store::transaction const & wallet_txn, wallet_handle const & handle, lumex::store::db_val const & key) const
{
	auto mdb_key = lumex::store::lmdb::to_mdb_val (key);
	MDB_val mdb_value{};
	auto status = mdb_get (environment.tx (wallet_txn), handle_for (handle), &mdb_key, &mdb_value);
	if (lumex::store::lmdb::success (status))
	{
		return lumex::store::lmdb::from_mdb_val (mdb_value);
	}
	return std::nullopt;
}

void wallets_backend_lmdb::entry_put (lumex::store::write_transaction const & wallet_txn, wallet_handle const & handle, lumex::store::db_val const & key, lumex::store::db_val const & value)
{
	auto mdb_key = lumex::store::lmdb::to_mdb_val (key);
	auto mdb_value = lumex::store::lmdb::to_mdb_val (value);
	auto status = mdb_put (environment.tx (wallet_txn), handle_for (handle), &mdb_key, &mdb_value, 0);
	release_assert (lumex::store::lmdb::success (status), lumex::store::lmdb::error_string (status));
}

void wallets_backend_lmdb::entry_del (lumex::store::write_transaction const & wallet_txn, wallet_handle const & handle, lumex::store::db_val const & key)
{
	auto mdb_key = lumex::store::lmdb::to_mdb_val (key);
	auto status = mdb_del (environment.tx (wallet_txn), handle_for (handle), &mdb_key, nullptr);
	release_assert (lumex::store::lmdb::success (status), lumex::store::lmdb::error_string (status));
}

bool wallets_backend_lmdb::entry_exists (lumex::store::transaction const & wallet_txn, wallet_handle const & handle, lumex::store::db_val const & key) const
{
	auto mdb_key = lumex::store::lmdb::to_mdb_val (key);
	MDB_val mdb_value{};
	auto status = mdb_get (environment.tx (wallet_txn), handle_for (handle), &mdb_key, &mdb_value);
	return lumex::store::lmdb::success (status);
}

lumex::store::iterator wallets_backend_lmdb::entries_begin (lumex::store::transaction const & wallet_txn, wallet_handle const & handle) const
{
	return lumex::store::iterator{ wallet_txn, lumex::store::lmdb::iterator::begin (environment.tx (wallet_txn), handle_for (handle)) };
}

lumex::store::iterator wallets_backend_lmdb::entries_begin (lumex::store::transaction const & wallet_txn, wallet_handle const & handle, lumex::store::db_val const & key) const
{
	return lumex::store::iterator{ wallet_txn, lumex::store::lmdb::iterator::lower_bound (environment.tx (wallet_txn), handle_for (handle), lumex::store::lmdb::to_mdb_val (key)) };
}

lumex::store::iterator wallets_backend_lmdb::entries_end (lumex::store::transaction const & wallet_txn, wallet_handle const & handle) const
{
	return lumex::store::iterator{ wallet_txn, lumex::store::lmdb::iterator::end (environment.tx (wallet_txn), handle_for (handle)) };
}

lumex::store::iterator wallets_backend_lmdb::index_begin (lumex::store::transaction const & wallet_txn) const
{
	return lumex::store::iterator{ wallet_txn, lumex::store::lmdb::iterator::begin (environment.tx (wallet_txn), wallet_index_dbi) };
}

lumex::store::iterator wallets_backend_lmdb::index_begin (lumex::store::transaction const & wallet_txn, lumex::store::db_val const & key) const
{
	return lumex::store::iterator{ wallet_txn, lumex::store::lmdb::iterator::lower_bound (environment.tx (wallet_txn), wallet_index_dbi, lumex::store::lmdb::to_mdb_val (key)) };
}

lumex::store::iterator wallets_backend_lmdb::index_end (lumex::store::transaction const & wallet_txn) const
{
	return lumex::store::iterator{ wallet_txn, lumex::store::lmdb::iterator::end (environment.tx (wallet_txn), wallet_index_dbi) };
}

std::optional<lumex::store::db_val> wallets_backend_lmdb::send_action_id_get (lumex::store::transaction const & wallet_txn, lumex::store::db_val const & id) const
{
	auto mdb_key = lumex::store::lmdb::to_mdb_val (id);
	MDB_val mdb_value{};
	auto status = mdb_get (environment.tx (wallet_txn), send_action_ids_dbi, &mdb_key, &mdb_value);
	if (lumex::store::lmdb::success (status))
	{
		return lumex::store::lmdb::from_mdb_val (mdb_value);
	}
	return std::nullopt;
}

bool wallets_backend_lmdb::send_action_id_put (lumex::store::write_transaction const & wallet_txn, lumex::store::db_val const & id, lumex::store::db_val const & value)
{
	auto mdb_key = lumex::store::lmdb::to_mdb_val (id);
	auto mdb_value = lumex::store::lmdb::to_mdb_val (value);
	auto status = mdb_put (environment.tx (wallet_txn), send_action_ids_dbi, &mdb_key, &mdb_value, 0);
	return lumex::store::lmdb::success (status);
}

void wallets_backend_lmdb::send_action_ids_clear (lumex::store::write_transaction const & wallet_txn)
{
	auto status = mdb_drop (environment.tx (wallet_txn), send_action_ids_dbi, 0);
	release_assert (lumex::store::lmdb::success (status), lumex::store::lmdb::error_string (status));
}

std::filesystem::path wallets_backend_lmdb::database_path () const
{
	return environment.database_path;
}

void wallets_backend_lmdb::backup (lumex::logger & logger) const
{
	environment.create_backup_file (environment.database_path, logger);
}

MDB_dbi wallets_backend_lmdb::handle_for (wallet_handle const & handle)
{
	release_assert (handle.valid (), "empty wallet handle");
	return static_cast<MDB_dbi> (handle.opaque ());
}
}
