#include <lumex/lib/files.hpp>
#include <lumex/node/daemonconfig.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/node_wrapper.hpp>

lumex::node_wrapper::node_wrapper (std::filesystem::path const & path_a, std::filesystem::path const & config_path_a, lumex::node_flags const & node_flags_a) :
	network_params{ lumex::get_active_network () },
	work{ network_params.network, 1 }
{
	/*
	 * @warning May throw a filesystem exception
	 */
	std::filesystem::create_directories (path_a);

	boost::system::error_code error_chmod;
	lumex::set_secure_perm_directory (path_a, error_chmod);

	lumex::daemon_config daemon_config{ path_a, network_params };
	auto error = lumex::read_node_config_toml (config_path_a, daemon_config, node_flags_a.config_overrides);
	if (error)
	{
		std::cerr << "Error deserializing config file";
		if (!node_flags_a.config_overrides.empty ())
		{
			std::cerr << " or --config option";
		}
		std::cerr << "\n"
				  << error.get_message () << std::endl;
		std::exit (1);
	}

	auto & node_config = daemon_config.node;
	node_config.peering_port = 24000;

	node = std::make_shared<lumex::node> (path_a, node_config, work, node_flags_a);
}

lumex::node_wrapper::~node_wrapper ()
{
	node->stop ();
}
