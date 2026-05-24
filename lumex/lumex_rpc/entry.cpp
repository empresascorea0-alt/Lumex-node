#include <lumex/lib/cli.hpp>
#include <lumex/lib/errors.hpp>
#include <lumex/lib/files.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/networks.hpp>
#include <lumex/lib/signal_manager.hpp>
#include <lumex/lib/thread_runner.hpp>
#include <lumex/lib/threading.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/lib/version.hpp>
#include <lumex/node/cli.hpp>
#include <lumex/node/ipc/ipc_server.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/rpc/rpc.hpp>
#include <lumex/rpc/rpc_request_processor.hpp>

#include <boost/program_options.hpp>

#include <latch>

using namespace lumex;

namespace
{
void run (std::filesystem::path const & data_path, std::vector<std::string> const & config_overrides)
{
	lumex::logger logger{ "rpc_daemon" };

	logger.info (lumex::log::type::daemon_rpc, "Daemon started (RPC)");

	std::filesystem::create_directories (data_path);

	boost::system::error_code error_chmod;
	lumex::set_secure_perm_directory (data_path, error_chmod);

	std::unique_ptr<lumex::thread_runner> runner;

	lumex::network_params network_params{ lumex::get_active_network () };
	lumex::rpc_config rpc_config{ network_params.network };
	auto error = lumex::read_rpc_config_toml (data_path, rpc_config, config_overrides);
	if (!error)
	{
		std::shared_ptr<boost::asio::io_context> io_ctx = std::make_shared<boost::asio::io_context> ();

		runner = std::make_unique<lumex::thread_runner> (io_ctx, logger, rpc_config.rpc_process.io_threads, lumex::thread_role::name::io_daemon);

		try
		{
			lumex::ipc_rpc_processor ipc_rpc_processor (io_ctx, rpc_config);
			auto rpc = lumex::get_rpc (io_ctx, rpc_config, ipc_rpc_processor);
			rpc->start ();

			std::atomic stopped{ false };

			auto signal_handler = [&stopped, &logger] (int signum) {
				logger.warn (lumex::log::type::daemon_rpc, "Interrupt signal received ({}), stopping...", lumex::to_signal_name (signum));
				stopped = true;
				stopped.notify_all ();
			};

			lumex::signal_manager sigman;
			sigman.register_signal_handler (SIGINT, signal_handler, true);
			sigman.register_signal_handler (SIGTERM, signal_handler, false);

			// Keep running until stopped flag is set
			stopped.wait (false);

			logger.info (lumex::log::type::daemon_rpc, "Stopping...");

			rpc->stop ();
			io_ctx->stop ();
			runner->join ();
		}
		catch (std::runtime_error const & e)
		{
			logger.critical (lumex::log::type::daemon_rpc, "Error while running RPC: {}", e.what ());
		}
	}
	else
	{
		logger.critical (lumex::log::type::daemon_rpc, "Error deserializing config: {}", error.get_message ());
	}

	logger.info (lumex::log::type::daemon_rpc, "Stopped");
}
}

int main (int argc, char * const * argv)
{
	lumex::set_umask (); // Make sure the process umask is set before any files are created
	lumex::initialize_file_descriptor_limit ();
	lumex::logger::initialize (lumex::log_config::cli_default ());

	boost::program_options::options_description description ("Command line options");

	// clang-format off
	description.add_options ()
		("help", "Print out options")
		("config", boost::program_options::value<std::vector<lumex::config_key_value_pair>>()->multitoken(), "Pass RPC configuration values. This takes precedence over any values in the configuration file. This option can be repeated multiple times.")
		("daemon", "Start RPC daemon")
		("data_path", boost::program_options::value<std::string> (), "Use the supplied path as the data directory")
		("network", boost::program_options::value<std::string> (), "Use the supplied network (live, test, beta or dev)")
		("version", "Prints out version");
	// clang-format on

	boost::program_options::variables_map vm;
	try
	{
		boost::program_options::store (boost::program_options::parse_command_line (argc, argv, description), vm);
	}
	catch (boost::program_options::error const & err)
	{
		std::cerr << err.what () << std::endl;
		return 1;
	}
	boost::program_options::notify (vm);

	auto network (vm.find ("network"));
	if (network != vm.end ())
	{
		auto parsed = lumex::parse_network (network->second.as<std::string> ());
		if (!parsed)
		{
			std::cerr << "Invalid network. Valid values are live, test, beta and dev." << std::endl;
			std::exit (1);
		}
		lumex::set_active_network (parsed.value ());
	}

	auto data_path_it = vm.find ("data_path");
	std::filesystem::path data_path ((data_path_it != vm.end ()) ? std::filesystem::path (data_path_it->second.as<std::string> ()) : lumex::working_path ());
	if (vm.count ("daemon") > 0)
	{
		std::vector<std::string> config_overrides;
		auto config (vm.find ("config"));
		if (config != vm.end ())
		{
			config_overrides = lumex::config_overrides (config->second.as<std::vector<lumex::config_key_value_pair>> ());
		}
		run (data_path, config_overrides);
	}
	else if (vm.count ("version"))
	{
		std::cout << "Version " << LUMEX_VERSION_STRING << "\n"
				  << "Build Info " << BUILD_INFO << std::endl;
	}
	else
	{
		// Issue #3748
		// Regardless how the options were added, output the options in alphabetical order so they are easy to find.
		boost::program_options::options_description sorted_description ("Command line options");
		lumex::sort_options_description (description, sorted_description);
		std::cout << sorted_description << std::endl;
	}

	return 1;
}
