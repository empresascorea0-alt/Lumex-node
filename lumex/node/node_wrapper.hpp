#pragma once

#include <lumex/lib/work.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/network_params.hpp>

#include <filesystem>
#include <memory>

namespace lumex
{

class node;
class node_flags;

class node_wrapper final
{
public:
	node_wrapper (std::filesystem::path const & path_a, std::filesystem::path const & config_path_a, lumex::node_flags const & node_flags_a);
	~node_wrapper ();

	lumex::network_params network_params;
	lumex::work_pool work;
	std::shared_ptr<lumex::node> node;
};

}
