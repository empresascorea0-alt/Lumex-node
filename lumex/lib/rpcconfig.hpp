#pragma once

#include <lumex/lib/config.hpp>
#include <lumex/lib/constants.hpp>
#include <lumex/lib/errors.hpp>
#include <lumex/lib/threading.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace lumex
{
class tomlconfig;

class rpc_process_config final
{
public:
	rpc_process_config (lumex::network_constants & network_constants);
	lumex::network_constants & network_constants;
	unsigned io_threads{ std::max (lumex::hardware_concurrency (), 4u) };
	std::string ipc_address;
	uint16_t ipc_port{ network_constants.default_ipc_port };
	unsigned num_ipc_connections{ (network_constants.is_live_network () || network_constants.is_test_network ()) ? 8u : network_constants.is_beta_network () ? 4u
																																							 : 1u };
};

class rpc_logging_config final
{
public:
	bool log_rpc{ true };
};

class rpc_config final
{
public:
	explicit rpc_config (lumex::network_constants & network_constants);
	explicit rpc_config (lumex::network_constants & network_constants, uint16_t, bool);
	lumex::error serialize_toml (lumex::tomlconfig &) const;
	lumex::error deserialize_toml (lumex::tomlconfig &);

	lumex::rpc_process_config rpc_process;
	std::string address;
	uint16_t port{ rpc_process.network_constants.default_rpc_port };
	bool enable_control{ false };
	uint8_t max_json_depth{ 20 };
	uint64_t max_request_size{ 32 * 1024 * 1024 };
	lumex::rpc_logging_config rpc_logging;
};

lumex::error read_rpc_config_toml (std::filesystem::path const & data_path_a, lumex::rpc_config & config_a, std::vector<std::string> const & config_overrides = std::vector<std::string> ());

std::string get_default_rpc_filepath ();
}
