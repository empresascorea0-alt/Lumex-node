#pragma once

#include <lumex/lib/errors.hpp>
#include <lumex/lib/threading.hpp>

#include <string>
#include <thread>

namespace lumex
{
class tomlconfig;

class rocksdb_config final
{
public:
	lumex::error serialize_toml (lumex::tomlconfig &) const;
	lumex::error deserialize_toml (lumex::tomlconfig &);

public:
	bool enable{ false };
	unsigned io_threads{ std::max (lumex::hardware_concurrency () / 2, 1u) };
	long read_cache{ 32 };
	long write_cache{ 64 };
	unsigned max_log_files{ 100 };
	std::string log_level{ "warn" };
};
}
