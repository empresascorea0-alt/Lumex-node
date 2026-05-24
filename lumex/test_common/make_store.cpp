#include <lumex/lib/files.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/node/make_store.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/store/ledger_store.hpp>
#include <lumex/store/lmdb/backend_lmdb.hpp>
#include <lumex/store/rocksdb/backend_rocksdb.hpp>
#include <lumex/test_common/common.hpp>
#include <lumex/test_common/make_store.hpp>

std::unique_ptr<lumex::store::backend> lumex::test::make_backend (std::filesystem::path path)
{
	path = path.empty () ? lumex::unique_path () : path;

	auto backend_type = lumex::default_database_backend ();
	switch (backend_type)
	{
		case lumex::database_backend::lmdb:
		{
			auto db_path = path / "data.ldb";
			lumex::lmdb_config lmdb_config{};
			return std::make_unique<lumex::store::lmdb::backend_lmdb> (db_path, lmdb_config, lumex::test::default_logger ());
		}
		case lumex::database_backend::rocksdb:
		{
			auto db_path = path / "rocksdb";
			lumex::rocksdb_config rocksdb_config{};
			return std::make_unique<lumex::store::rocksdb::backend_rocksdb> (db_path, rocksdb_config, lumex::test::default_logger ());
		}
	}
	release_assert (false, "unknown database backend");
}

std::unique_ptr<lumex::store::ledger_store> lumex::test::make_store (std::filesystem::path path)
{
	return lumex::test::make_store (lumex::test::default_logger (), lumex::test::default_stats (), path);
}

std::unique_ptr<lumex::store::ledger_store> lumex::test::make_store (lumex::logger & logger, lumex::stats & stats, std::filesystem::path path)
{
	path = path.empty () ? lumex::unique_path () : path;

	return lumex::make_store (logger, stats, path, lumex::dev::constants, false, true, lumex::node_config{});
}
