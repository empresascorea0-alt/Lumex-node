#include <lumex/node/active_elections.hpp>
#include <lumex/node/inactive_node.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/nodeconfig.hpp>

lumex::inactive_node::inactive_node (std::filesystem::path const & path_a, std::filesystem::path const & config_path_a, lumex::node_flags const & node_flags_a) :
	node_wrapper (path_a, config_path_a, node_flags_a),
	node (node_wrapper.node)
{
	node_wrapper.node->active.stop ();
}

lumex::inactive_node::inactive_node (std::filesystem::path const & path_a, lumex::node_flags const & node_flags_a) :
	inactive_node (path_a, path_a, node_flags_a)
{
}

lumex::node_flags const & lumex::inactive_node_flag_defaults ()
{
	static lumex::node_flags node_flags;
	node_flags.inactive_node = true;
	node_flags.read_only = true;
	node_flags.generate_cache = lumex::generate_cache_flags::all_disabled ();
	node_flags.disable_bootstrap_listener = true;
	node_flags.disable_tcp_realtime = true;
	return node_flags;
}
