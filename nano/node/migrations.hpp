#pragma once

#include <nano/lib/lmdbconfig.hpp>
#include <nano/lib/rocksdbconfig.hpp>

#include <filesystem>

namespace nano
{
/**
 * Migrate LMDB database to RocksDB.
 * @throws std::runtime_error on failure
 */
void migrate_lmdb_to_rocksdb (
std::filesystem::path const & data_path,
nano::lmdb_config const & lmdb_config = {},
nano::rocksdb_config const & rocksdb_config = {});
}
