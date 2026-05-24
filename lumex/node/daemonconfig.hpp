#pragma once

#include <lumex/lib/errors.hpp>
#include <lumex/node/node_rpc_config.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/openclconfig.hpp>

#include <vector>

namespace lumex
{
class tomlconfig;
class daemon_config
{
public:
	daemon_config () = default;
	daemon_config (std::filesystem::path const & data_path, lumex::network_params & network_params);
	lumex::error deserialize_toml (lumex::tomlconfig &);
	lumex::error serialize_toml (lumex::tomlconfig &);
	bool rpc_enable{ false };
	lumex::node_rpc_config rpc;
	lumex::node_config node;
	bool opencl_enable{ false };
	lumex::opencl_config opencl;
	std::filesystem::path data_path;
};

lumex::error read_node_config_toml (std::filesystem::path const &, lumex::daemon_config & config_a, std::vector<std::string> const & config_overrides = std::vector<std::string> ());
}
