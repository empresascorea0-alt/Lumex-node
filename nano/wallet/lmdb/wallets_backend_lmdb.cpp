#include <nano/lib/logging.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/store/lmdb/common.hpp>
#include <nano/store/lmdb/db_val.hpp>
#include <nano/store/lmdb/iterator.hpp>
#include <nano/store/lmdb/utility.hpp>
#include <nano/wallet/lmdb/wallets_backend_lmdb.hpp>

#include <stdexcept>

namespace nano::wallet::lmdb
{
wallets_backend_lmdb::wallets_backend_lmdb (std::filesystem::path const & path, nano::lmdb_config const & config) :
	environment (path, nano::store::lmdb::env::options::make ().set_config (config).override_config_sync (nano::lmdb_config::sync_strategy::always).override_config_map_size (1ULL * 1024 * 1024 * 1024))
{
	auto wallet_txn = environment.tx_begin_write ();
	auto status1 = mdb_dbi_open (environment.tx (wallet_txn), nullptr, MDB_CREATE, &wallet_index_dbi);
	release_assert (nano::store::lmdb::success (status1), nano::store::lmdb::error_string (status1));
	auto status2 = mdb_dbi_open (environment.tx (wallet_txn), send_action_ids_name.data (), MDB_CREATE, &send_action_ids_dbi);
	release_assert (nano::store::lmdb::success (status2), nano::store::lmdb::error_string (status2));
}

nano::store::read_transaction wallets_backend_lmdb::tx_begin_read () const
{
	return environment.tx_begin_read ();
}

nano::store::write_transaction wallets_backend_lmdb::tx_begin_write ()
{
	return environment.tx_begin_write ();
}

wallet_handle wallets_backend_lmdb::wallet_open_or_create (nano::store::write_transaction const & wallet_txn, std::string const & wallet_id_hex)
{
	MDB_dbi dbi{};
	auto status = mdb_dbi_open (environment.tx (wallet_txn), wallet_id_hex.c_str (), MDB_CREATE, &dbi);
	if (!nano::store::lmdb::success (status))
	{
		throw std::runtime_error ("Failed to open wallet database '" + wallet_id_hex + "': " + nano::store::lmdb::error_string (status));
	}
	return wallet_handle{ dbi };
}

void wallets_backend_lmdb::wallet_drop (nano::store::write_transaction const & wallet_txn, wallet_handle & handle)
{
	if (!handle)
	{
		return;
	}
	auto status = mdb_drop (environment.tx (wallet_txn), handle_for (handle), /* delete from database */ 1);
	release_assert (nano::store::lmdb::success (status), nano::store::lmdb::error_string (status));
	handle = wallet_handle{};
}

std::optional<nano::store::db_val> wallets_backend_lmdb::entry_get (nano::store::transaction const & wallet_txn, wallet_handle const & handle, nano::store::db_val const & key) const
{
	auto mdb_key = nano::store::lmdb::to_mdb_val (key);
	MDB_val mdb_value{};
	auto status = mdb_get (environment.tx (wallet_txn), handle_for (handle), &mdb_key, &mdb_value);
	if (nano::store::lmdb::success (status))
	{
		return nano::store::lmdb::from_mdb_val (mdb_value);
	}
	return std::nullopt;
}

void wallets_backend_lmdb::entry_put (nano::store::write_transaction const & wallet_txn, wallet_handle const & handle, nano::store::db_val const & key, nano::store::db_val const & value)
{
	auto mdb_key = nano::store::lmdb::to_mdb_val (key);
	auto mdb_value = nano::store::lmdb::to_mdb_val (value);
	auto status = mdb_put (environment.tx (wallet_txn), handle_for (handle), &mdb_key, &mdb_value, 0);
	release_assert (nano::store::lmdb::success (status), nano::store::lmdb::error_string (status));
}

void wallets_backend_lmdb::entry_del (nano::store::write_transaction const & wallet_txn, wallet_handle const & handle, nano::store::db_val const & key)
{
	auto mdb_key = nano::store::lmdb::to_mdb_val (key);
	auto status = mdb_del (environment.tx (wallet_txn), handle_for (handle), &mdb_key, nullptr);
	release_assert (nano::store::lmdb::success (status), nano::store::lmdb::error_string (status));
}

bool wallets_backend_lmdb::entry_exists (nano::store::transaction const & wallet_txn, wallet_handle const & handle, nano::store::db_val const & key) const
{
	auto mdb_key = nano::store::lmdb::to_mdb_val (key);
	MDB_val mdb_value{};
	auto status = mdb_get (environment.tx (wallet_txn), handle_for (handle), &mdb_key, &mdb_value);
	return nano::store::lmdb::success (status);
}

nano::store::iterator wallets_backend_lmdb::entries_begin (nano::store::transaction const & wallet_txn, wallet_handle const & handle) const
{
	return nano::store::iterator{ wallet_txn, nano::store::lmdb::iterator::begin (environment.tx (wallet_txn), handle_for (handle)) };
}

nano::store::iterator wallets_backend_lmdb::entries_begin (nano::store::transaction const & wallet_txn, wallet_handle const & handle, nano::store::db_val const & key) const
{
	return nano::store::iterator{ wallet_txn, nano::store::lmdb::iterator::lower_bound (environment.tx (wallet_txn), handle_for (handle), nano::store::lmdb::to_mdb_val (key)) };
}

nano::store::iterator wallets_backend_lmdb::entries_end (nano::store::transaction const & wallet_txn, wallet_handle const & handle) const
{
	return nano::store::iterator{ wallet_txn, nano::store::lmdb::iterator::end (environment.tx (wallet_txn), handle_for (handle)) };
}

nano::store::iterator wallets_backend_lmdb::index_begin (nano::store::transaction const & wallet_txn) const
{
	return nano::store::iterator{ wallet_txn, nano::store::lmdb::iterator::begin (environment.tx (wallet_txn), wallet_index_dbi) };
}

nano::store::iterator wallets_backend_lmdb::index_begin (nano::store::transaction const & wallet_txn, nano::store::db_val const & key) const
{
	return nano::store::iterator{ wallet_txn, nano::store::lmdb::iterator::lower_bound (environment.tx (wallet_txn), wallet_index_dbi, nano::store::lmdb::to_mdb_val (key)) };
}

nano::store::iterator wallets_backend_lmdb::index_end (nano::store::transaction const & wallet_txn) const
{
	return nano::store::iterator{ wallet_txn, nano::store::lmdb::iterator::end (environment.tx (wallet_txn), wallet_index_dbi) };
}

std::optional<nano::store::db_val> wallets_backend_lmdb::send_action_id_get (nano::store::transaction const & wallet_txn, nano::store::db_val const & id) const
{
	auto mdb_key = nano::store::lmdb::to_mdb_val (id);
	MDB_val mdb_value{};
	auto status = mdb_get (environment.tx (wallet_txn), send_action_ids_dbi, &mdb_key, &mdb_value);
	if (nano::store::lmdb::success (status))
	{
		return nano::store::lmdb::from_mdb_val (mdb_value);
	}
	return std::nullopt;
}

bool wallets_backend_lmdb::send_action_id_put (nano::store::write_transaction const & wallet_txn, nano::store::db_val const & id, nano::store::db_val const & value)
{
	auto mdb_key = nano::store::lmdb::to_mdb_val (id);
	auto mdb_value = nano::store::lmdb::to_mdb_val (value);
	auto status = mdb_put (environment.tx (wallet_txn), send_action_ids_dbi, &mdb_key, &mdb_value, 0);
	return nano::store::lmdb::success (status);
}

void wallets_backend_lmdb::send_action_ids_clear (nano::store::write_transaction const & wallet_txn)
{
	auto status = mdb_drop (environment.tx (wallet_txn), send_action_ids_dbi, 0);
	release_assert (nano::store::lmdb::success (status), nano::store::lmdb::error_string (status));
}

std::filesystem::path wallets_backend_lmdb::database_path () const
{
	return environment.database_path;
}

void wallets_backend_lmdb::backup (nano::logger & logger) const
{
	environment.create_backup_file (environment.database_path, logger);
}

MDB_dbi wallets_backend_lmdb::handle_for (wallet_handle const & handle)
{
	release_assert (handle.valid (), "empty wallet handle");
	return static_cast<MDB_dbi> (handle.opaque ());
}
}
