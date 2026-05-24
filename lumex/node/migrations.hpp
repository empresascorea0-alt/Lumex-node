#pragma once

#include <lumex/lib/lmdbconfig.hpp>
#include <lumex/lib/rocksdbconfig.hpp>

#include <filesystem>

namespace lumex
{
/**
 * Migrate LMDB database to RocksDB.
 * @throws std::runtime_error on failure
 */
void migrate_lmdb_to_rocksdb (
std::filesystem::path const & data_path,
lumex::lmdb_config const & lmdb_config = {},
lumex::rocksdb_config const & rocksdb_config = {});
}
