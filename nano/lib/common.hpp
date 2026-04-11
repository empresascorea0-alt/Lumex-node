#pragma once

#include <optional>
#include <string>

namespace nano
{
enum class database_backend
{
	lmdb,
	rocksdb
};

std::string to_string (database_backend);
std::optional<database_backend> parse_database_backend (std::string);
}
