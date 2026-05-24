#include <lumex/lib/logging.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/node/make_store.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/store/ledger_store.hpp>
#include <lumex/store/lmdb/backend_lmdb.hpp>
#include <lumex/store/rocksdb/backend_rocksdb.hpp>

std::filesystem::path lumex::database_path_for_backend (std::filesystem::path const & base_path, database_backend backend_type)
{
	switch (backend_type)
	{
		case lumex::database_backend::lmdb:
			return base_path / "data.ldb";
		case lumex::database_backend::rocksdb:
			return base_path / "rocksdb";
	}
	release_assert (false, "unknown database backend");
}

std::unique_ptr<lumex::store::ledger_store> lumex::make_store (lumex::logger & logger, lumex::stats & stats, std::filesystem::path const & path, lumex::ledger_constants & constants, bool read_only, bool add_db_postfix, lumex::node_config const & node_config)
{
	auto decide_backend = [&] () -> lumex::database_backend {
		if (node_config.rocksdb_config->enable && node_config.database_backend == lumex::database_backend::lmdb)
		{
			logger.warn (lumex::log::type::config, "Use of deprecated `[node.rocksdb].enable` setting detected in config file, defaulting to RocksDB backend.\nPlease edit config-node.toml and use the new '[node].database_backend' for future compatibility.");
			return lumex::database_backend::rocksdb;
		}
		return node_config.database_backend;
	};

	lumex::store::open_mode const mode = read_only ? lumex::store::open_mode::read_only : lumex::store::open_mode::read_write;

	auto backend_type = decide_backend ();
	std::unique_ptr<lumex::store::backend> backend;

	switch (backend_type)
	{
		case lumex::database_backend::lmdb:
		{
			auto db_path = add_db_postfix ? database_path_for_backend (path, backend_type) : path;
			backend = std::make_unique<lumex::store::lmdb::backend_lmdb> (db_path, *node_config.lmdb_config, logger, *node_config.txn_tracking);
			break;
		}
		case lumex::database_backend::rocksdb:
		{
			auto db_path = add_db_postfix ? database_path_for_backend (path, backend_type) : path;
			backend = std::make_unique<lumex::store::rocksdb::backend_rocksdb> (db_path, *node_config.rocksdb_config, logger, *node_config.txn_tracking);
			break;
		}
	}

	release_assert (backend != nullptr);

	lumex::store::ledger_store_params params;
	params.backup_before_upgrade = node_config.backup_before_upgrade;

	return std::make_unique<lumex::store::ledger_store> (std::move (backend), mode, stats, logger, params);
}
