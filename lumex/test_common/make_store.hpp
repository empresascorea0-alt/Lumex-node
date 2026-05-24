#pragma once

#include <lumex/lib/fwd.hpp>
#include <lumex/store/fwd.hpp>

#include <filesystem>
#include <memory>

/**
 * Helper functions to create a backend directly for testing
 */
namespace lumex::test
{
std::unique_ptr<lumex::store::backend> make_backend (std::filesystem::path path = {});
std::unique_ptr<lumex::store::ledger_store> make_store (std::filesystem::path path = {});
std::unique_ptr<lumex::store::ledger_store> make_store (lumex::logger &, lumex::stats &, std::filesystem::path path = {});
}
