#pragma once

#include <lumex/lib/fwd.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/secure/fwd.hpp>
#include <lumex/store/fwd.hpp>

#include <filesystem>
#include <memory>

namespace lumex
{

// Returns the database path for the given backend type
std::filesystem::path database_path_for_backend (std::filesystem::path const & base_path, database_backend backend_type);

std::unique_ptr<lumex::store::ledger_store> make_store (
lumex::logger &,
lumex::stats &,
std::filesystem::path const & path,
lumex::ledger_constants & constants,
bool read_only,
bool add_db_postfix,
lumex::node_config const & node_config);
}
