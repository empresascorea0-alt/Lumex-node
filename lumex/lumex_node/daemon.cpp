#include <lumex/boost/process/child.hpp>
#include <lumex/lib/files.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/memory.hpp>
#include <lumex/lib/runtime_files.hpp>
#include <lumex/lib/signal_manager.hpp>
#include <lumex/lib/stacktrace.hpp>
#include <lumex/lib/thread_runner.hpp>
#include <lumex/lib/threading.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/lib/version.hpp>
#include <lumex/lib/work.hpp>
#include <lumex/lumex_node/daemon.hpp>
#include <lumex/node/cli.hpp>
#include <lumex/node/daemonconfig.hpp>
#include <lumex/node/ipc/ipc_server.hpp>
#include <lumex/node/json_handler.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/node_scope_guard.hpp>
#include <lumex/node/openclwork.hpp>
#include <lumex/rpc/rpc.hpp>

#include <csignal>
#include <iostream>
#include <latch>

#include <fmt/chrono.h>

namespace
{
void lumex_abort_signal_handler (int signum)
{
	// remove `signum` from signal handling when under Windows
#ifdef _WIN32
	std::signal (signum, SIG_DFL);
#endif

	// create some debugging log files
	lumex::dump_crash_stacktrace ();
	lumex::create_load_memory_address_files ();

	// re-raise signal to call the default handler and exit
	raise (signum);
}

void install_abort_signal_handler ()
{
	// We catch signal SIGSEGV and SIGABRT not via the signal manager because we want these signal handlers
	// to be executed in the stack of the code that caused the signal, so we can dump the stacktrace.
#ifdef _WIN32
	std::signal (SIGSEGV, lumex_abort_signal_handler);
	std::signal (SIGABRT, lumex_abort_signal_handler);
#else
	struct sigaction sa = {};
	sa.sa_handler = lumex_abort_signal_handler;
	sigemptyset (&sa.sa_mask);
	sa.sa_flags = SA_RESETHAND;
	sigaction (SIGSEGV, &sa, NULL);
	sigaction (SIGABRT, &sa, NULL);
#endif
}
}

void lumex::daemon::run (std::filesystem::path const & data_path, lumex::node_flags const & flags)
{
	lumex::logger::initialize (lumex::log_config::daemon_default (), data_path, flags.config_overrides);

	install_abort_signal_handler ();

	logger.info (lumex::log::type::daemon, "Daemon started");
	logger.info (lumex::log::type::daemon, "Version: {}", LUMEX_VERSION_STRING);
	logger.info (lumex::log::type::daemon, "Build info: {}", BUILD_INFO);
	logger.info (lumex::log::type::daemon, "Data path: {}", data_path.string ());

	std::time_t dateTime = std::time (nullptr);
	logger.info (lumex::log::type::daemon, "Start time: {:%c} UTC", fmt::gmtime (dateTime));

	std::filesystem::create_directories (data_path);
	boost::system::error_code error_chmod;
	lumex::set_secure_perm_directory (data_path, error_chmod);

	std::unique_ptr<lumex::thread_runner> runner;

	lumex::network_params network_params{ lumex::get_active_network () };
	lumex::daemon_config config{ data_path, network_params };
	if (auto error = lumex::read_node_config_toml (data_path, config, flags.config_overrides))
	{
		logger.critical (lumex::log::type::daemon, "Error deserializing node config: {}", error.get_message ());
		std::exit (1);
	}
	if (auto error = lumex::flags_config_conflicts (flags, config.node))
	{
		logger.critical (lumex::log::type::daemon, "Error parsing command line options: {}", error.message ());
		std::exit (1);
	}

	lumex::set_use_memory_pools (config.node.use_memory_pools);

	std::shared_ptr<boost::asio::io_context> io_ctx = std::make_shared<boost::asio::io_context> ();

	auto opencl = lumex::opencl_work::create (config.opencl_enable, config.opencl, logger, config.node.network_params.work);
	lumex::opencl_work_func_t opencl_work_func;
	if (opencl)
	{
		opencl_work_func = [&opencl] (lumex::work_version const version_a, lumex::root const & root_a, uint64_t difficulty_a, std::atomic<int> & ticket_a) {
			return opencl->generate_work (version_a, root_a, difficulty_a, ticket_a);
		};
	}
	lumex::work_pool opencl_work (config.node.network_params.network, config.node.work_threads, config.node.pow_sleep_interval, opencl_work_func);
	try
	{
		// This avoids a blank prompt during any node initialization delays
		logger.info (lumex::log::type::daemon, "Starting up Lumex node...");

		// Print info about number of logical cores detected, those are used to decide how many IO, worker and signature checker threads to spawn
		logger.info (lumex::log::type::daemon, "Hardware concurrency: {} (configured: {})", std::thread::hardware_concurrency (), lumex::hardware_concurrency ());
		auto const file_descriptor_limits = lumex::get_file_descriptor_limit ();
		logger.info (lumex::log::type::daemon, "File descriptor limit: {} (hard cap: {})", file_descriptor_limits.soft_limit, file_descriptor_limits.hard_limit);

		// for the daemon start up, if the user hasn't specified a port in
		// the config, we must use the default peering port for the network
		//
		if (!config.node.peering_port.has_value ())
		{
			config.node.peering_port = network_params.network.default_node_port;
		}

		// Use a scope guard to ensure node->stop() is called while we still hold a shared_ptr (even in case of exceptions)
		lumex::node_scope_guard node{ std::make_shared<lumex::node> (data_path, config.node, opencl_work, flags) };

		// IO context runner should be started first and stopped last to allow asio handlers to execute during node start/stop
		runner = std::make_unique<lumex::thread_runner> (io_ctx, logger, node->config.io_threads, lumex::thread_role::name::io_daemon);

		node->start ();

		std::atomic stopped{ false };

		std::unique_ptr<lumex::ipc::ipc_server> ipc_server = std::make_unique<lumex::ipc::ipc_server> (*node, config.rpc);
		std::unique_ptr<boost::process::child> rpc_process;
		std::unique_ptr<lumex::rpc_handler_interface> rpc_handler;
		std::shared_ptr<lumex::rpc> rpc;

		bool const rpc_enabled = config.rpc_enable || flags.enable_rpc;
		if (rpc_enabled)
		{
			// In process RPC
			if (!config.rpc.child_process.enable)
			{
				auto stop_callback = [this, &stopped] () {
					logger.warn (lumex::log::type::daemon, "RPC stop request received, stopping...");
					stopped = true;
					stopped.notify_all ();
				};

				// Launch rpc in-process
				lumex::rpc_config rpc_config{ config.node.network_params.network };
				if (auto error = lumex::read_rpc_config_toml (data_path, rpc_config, flags.rpc_config_overrides))
				{
					logger.critical (lumex::log::type::daemon, "Error deserializing RPC config: {}", error.get_message ());
					std::exit (1);
				}

				logger.debug (lumex::log::type::daemon, "Starting in-process RPC server on port {}", rpc_config.port);

				rpc_handler = std::make_unique<lumex::inprocess_rpc_handler> (*node, *ipc_server, config.rpc, stop_callback);
				rpc = lumex::get_rpc (io_ctx, rpc_config, *rpc_handler);
				rpc->start ();
			}
			else
			{
				// Spawn a child rpc process
				if (!std::filesystem::exists (config.rpc.child_process.rpc_path))
				{
					throw std::runtime_error (std::string ("RPC is configured to spawn a new process however the file cannot be found at: ") + config.rpc.child_process.rpc_path);
				}

				logger.warn (lumex::log::type::daemon, "RPC is configured to run in a separate process, this is experimental and is not recommended for production use. Please consider using the in-process RPC instead.");

				logger.debug (lumex::log::type::daemon, "Spawning RPC process with command: {}", config.rpc.child_process.rpc_path);

				std::string network{ node->network_params.network.get_current_network_as_string () };
				rpc_process = std::make_unique<boost::process::child> (config.rpc.child_process.rpc_path, "--daemon", "--data_path", data_path.string (), "--network", network);
			}
			debug_assert (rpc || rpc_process);
		}

		// Write runtime info file with actual bound ports (if requested)
		if (flags.runtime_info_file)
		{
			lumex::runtime_files::runtime_info info;
			info.peering_port = node->network.endpoint ().port ();
			info.node_id = node->get_node_id ().to_node_id ();
			// Only set if rpc is enabled
			if (rpc)
			{
				info.rpc_port = rpc->listening_port ();
			}
			lumex::runtime_files::create_runtime_info (*flags.runtime_info_file, info);
			std::cerr << "Runtime info file created: " << *flags.runtime_info_file << std::endl;
		}

		auto signal_handler = [this, &stopped] (int signum) {
			logger.warn (lumex::log::type::daemon, "Interrupt signal received ({}), stopping...", to_signal_name (signum));
			stopped = true;
			stopped.notify_all ();
		};

		lumex::signal_manager sigman;
		// keep trapping Ctrl-C to avoid a second Ctrl-C interrupting tasks started by the first
		sigman.register_signal_handler (SIGINT, signal_handler, true);
		// sigterm is less likely to come in bunches so only trap it once
		sigman.register_signal_handler (SIGTERM, signal_handler, false);

		// Keep running until stopped flag is set
		stopped.wait (false);

		logger.info (lumex::log::type::daemon, "Stopping...");

		if (rpc)
		{
			rpc->stop ();
		}
		ipc_server->stop ();
		node->stop ();
		io_ctx->stop ();
		runner->join ();

		if (rpc_process)
		{
			rpc_process->wait ();
		}
	}
	catch (std::exception const & ex)
	{
		logger.critical (lumex::log::type::daemon, "Error: {}", ex.what ());
		std::exit (1);
	}

	logger.info (lumex::log::type::daemon, "Stopped");
}
