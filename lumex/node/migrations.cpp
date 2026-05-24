#include <lumex/lib/config.hpp>
#include <lumex/lib/files.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/node/make_store.hpp>
#include <lumex/node/migrations.hpp>
#include <lumex/store/ledger_store.hpp>
#include <lumex/store/lmdb/backend_lmdb.hpp>
#include <lumex/store/rocksdb/backend_rocksdb.hpp>

void lumex::migrate_lmdb_to_rocksdb (
std::filesystem::path const & data_path,
lumex::lmdb_config const & lmdb_config,
lumex::rocksdb_config const & rocksdb_config)
{
	lumex::default_logger ().info (lumex::log::type::migration, "Migrating LMDB database to RocksDB. This will take a while...");

	auto lmdb_path = lumex::database_path_for_backend (data_path, lumex::database_backend::lmdb);
	auto rocksdb_path = lumex::database_path_for_backend (data_path, lumex::database_backend::rocksdb);

	// Check source exists
	if (!std::filesystem::exists (lmdb_path))
	{
		throw std::runtime_error ("LMDB database not found: " + lmdb_path.string ());
	}

	// Check destination doesn't already exist
	if (std::filesystem::exists (rocksdb_path))
	{
		lumex::default_logger ().error (lumex::log::type::migration, "Existing RocksDB folder found in '{}'. Please remove it and try again.", rocksdb_path.string ());
		throw std::runtime_error ("RocksDB folder already exists: " + rocksdb_path.string ());
	}

	// Check disk space
	std::filesystem::space_info si = std::filesystem::space (data_path);
	auto file_size = std::filesystem::file_size (lmdb_path);
	auto const estimated_required_space = static_cast<uint64_t> (file_size * 0.65);

	if (si.available < estimated_required_space)
	{
		lumex::default_logger ().warn (lumex::log::type::migration, "You may not have enough available disk space. Estimated free space requirement is {} GB", estimated_required_space / 1024 / 1024 / 1024);
	}

	// Set secure permissions
	boost::system::error_code error_chmod;
	lumex::set_secure_perm_directory (data_path, error_chmod);

	// Create and open source LMDB backend (read-only)
	auto lmdb_backend = std::make_unique<lumex::store::lmdb::backend_lmdb> (lmdb_path, lmdb_config, lumex::default_logger ());
	lmdb_backend->open (lumex::store::ledger_store::schema_current, lumex::store::open_mode::read_only);

	// Create and open destination RocksDB backend (read-write)
	auto rocksdb_backend = std::make_unique<lumex::store::rocksdb::backend_rocksdb> (rocksdb_path, rocksdb_config, lumex::default_logger ());
	rocksdb_backend->open (lumex::store::ledger_store::schema_current, lumex::store::open_mode::read_write);

	auto progress_cb = [] (lumex::store::copy_progress const & p) {
		auto pct = p.total_entries > 0 ? p.entries_copied * 100 / p.total_entries : 100;

		lumex::default_logger ().info (lumex::log::type::migration,
		"Table {} of {}: {} - {:9} / {:9} ({}%)",
		p.current_table_index + 1, p.total_tables, p.table_name,
		p.entries_copied, p.total_entries, pct);
	};

	// Perform the migration
	lmdb_backend->copy_to (*rocksdb_backend, progress_cb);

	lumex::default_logger ().info (lumex::log::type::migration, "Migration completed. Make sure to set `database_backend` under [node] to 'rocksdb' in config-node.toml");
	lumex::default_logger ().info (lumex::log::type::migration, "After confirming correct node operation, the data.ldb file can be deleted if no longer required");
}
