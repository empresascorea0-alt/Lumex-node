#include <lumex/boost/process/child.hpp>
#include <lumex/crypto_lib/random_pool.hpp>
#include <lumex/lib/cli.hpp>
#include <lumex/lib/errors.hpp>
#include <lumex/lib/files.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/memory.hpp>
#include <lumex/lib/networks.hpp>
#include <lumex/lib/rpcconfig.hpp>
#include <lumex/lib/thread_runner.hpp>
#include <lumex/lib/tomlconfig.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/lib/walletconfig.hpp>
#include <lumex/lib/work.hpp>
#include <lumex/lumex_wallet/icon.hpp>
#include <lumex/node/cli.hpp>
#include <lumex/node/daemonconfig.hpp>
#include <lumex/node/ipc/ipc_server.hpp>
#include <lumex/node/json_handler.hpp>
#include <lumex/node/node_rpc_config.hpp>
#include <lumex/node/node_scope_guard.hpp>
#include <lumex/node/openclwork.hpp>
#include <lumex/node/wallet.hpp>
#include <lumex/qt/qt.hpp>
#include <lumex/rpc/rpc.hpp>

#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

namespace lumex
{
class wallet_daemon final
{
	lumex::logger logger{ "wallet_daemon" };

public:
	void show_error (std::string const & message_a)
	{
		logger.critical (lumex::log::type::daemon, "{}", message_a);

		QMessageBox message (QMessageBox::Critical, "Error starting Lumex", message_a.c_str ());
		message.setModal (true);
		message.show ();
		message.exec ();
	}

	void show_help (std::string const & message_a)
	{
		QMessageBox message (QMessageBox::NoIcon, "Help", "see <a href=\"https://docs.lumex.org/commands/command-line-interface/#launch-options\">launch options</a> ");
		message.setStyleSheet ("QLabel {min-width: 450px}");
		message.setDetailedText (message_a.c_str ());
		message.show ();
		message.exec ();
	}

	lumex::error write_wallet_config (lumex::wallet_config & config_a, std::filesystem::path const & data_path_a)
	{
		lumex::tomlconfig wallet_config_toml;
		auto wallet_path (lumex::get_qtwallet_toml_config_path (data_path_a));
		config_a.serialize_toml (wallet_config_toml);

		// Write wallet config. If missing, the file is created and permissions are set.
		wallet_config_toml.write (wallet_path);
		return wallet_config_toml.get_error ();
	}

	lumex::error read_wallet_config (lumex::wallet_config & config_a, std::filesystem::path const & data_path_a)
	{
		lumex::tomlconfig wallet_config_toml;
		auto wallet_path (lumex::get_qtwallet_toml_config_path (data_path_a));
		if (!std::filesystem::exists (wallet_path))
		{
			write_wallet_config (config_a, data_path_a);
		}
		wallet_config_toml.read (wallet_path);
		config_a.deserialize_toml (wallet_config_toml);
		return wallet_config_toml.get_error ();
	}

	int run_wallet (QApplication & application, int argc, char * const * argv, std::filesystem::path const & data_path, lumex::node_flags const & flags)
	{
		lumex::logger::initialize (lumex::log_config::daemon_default (), data_path, flags.config_overrides);

		logger.info (lumex::log::type::daemon_wallet, "Daemon started (wallet)");

		int result (0);
		lumex_qt::eventloop_processor processor;
		boost::system::error_code error_chmod;
		std::filesystem::create_directories (data_path);
		lumex::set_secure_perm_directory (data_path, error_chmod);
		QPixmap pixmap (":/logo.png");
		auto * splash = new QSplashScreen (pixmap);
		splash->show ();
		QApplication::processEvents ();
		splash->showMessage (QSplashScreen::tr ("Remember - Back Up Your Wallet Seed"), Qt::AlignBottom | Qt::AlignHCenter, Qt::darkGray);
		QApplication::processEvents ();

		lumex::network_params network_params{ lumex::get_active_network () };
		lumex::daemon_config config{ data_path, network_params };
		lumex::wallet_config wallet_config;

		auto error = lumex::read_node_config_toml (data_path, config, flags.config_overrides);
		if (!error)
		{
			error = read_wallet_config (wallet_config, data_path);
		}

		if (!error)
		{
			error = lumex::flags_config_conflicts (flags, config.node);
		}

		if (!error)
		{
			lumex::set_use_memory_pools (config.node.use_memory_pools);

			std::shared_ptr<boost::asio::io_context> io_ctx = std::make_shared<boost::asio::io_context> ();

			lumex::thread_runner runner (io_ctx, logger, config.node.io_threads, lumex::thread_role::name::io_daemon);

			try
			{
				std::shared_ptr<lumex_qt::wallet> gui;
				lumex::set_application_icon (application);
				auto opencl = lumex::opencl_work::create (config.opencl_enable, config.opencl, logger, config.node.network_params.work);
				lumex::opencl_work_func_t opencl_work_func;
				if (opencl)
				{
					opencl_work_func = [&opencl] (lumex::work_version const version_a, lumex::root const & root_a, uint64_t difficulty_a, std::atomic<int> &) {
						return opencl->generate_work (version_a, root_a, difficulty_a);
					};
				}
				lumex::work_pool work{ config.node.network_params.network, config.node.work_threads, config.node.pow_sleep_interval, opencl_work_func };
				lumex::node_scope_guard node{ std::make_shared<lumex::node> (data_path, config.node, work, flags) };
				auto wallet (node->wallets.open (wallet_config.wallet));
				if (wallet == nullptr)
				{
					auto existing (node->wallets.items.begin ());
					if (existing != node->wallets.items.end ())
					{
						wallet = existing->second;
						wallet_config.wallet = existing->first;
					}
					else
					{
						wallet = node->wallets.create (wallet_config.wallet);
					}
				}
				if (wallet_config.account.is_zero () || !wallet->exists (wallet_config.account))
				{
					auto wallet_accounts = wallet->accounts ();
					if (!wallet_accounts.empty ())
					{
						wallet_config.account = wallet_accounts.front ();
					}
					else
					{
						auto insert_result = wallet->deterministic_insert ();
						if (!insert_result)
						{
							splash->hide ();
							show_error ("Unable to create initial wallet account: " + insert_result.error ().get_message ());
							std::exit (1);
						}
						wallet_config.account = insert_result.value ();
					}
				}

				debug_assert (wallet->exists (wallet_config.account));
				write_wallet_config (wallet_config, data_path);
				node->start ();
				lumex::ipc::ipc_server ipc (*node, config.rpc);

				std::unique_ptr<boost::process::child> rpc_process;
				std::shared_ptr<lumex::rpc> rpc;
				std::unique_ptr<lumex::rpc_handler_interface> rpc_handler;
				bool const rpc_enabled = config.rpc_enable || flags.enable_rpc;
				if (rpc_enabled)
				{
					if (!config.rpc.child_process.enable)
					{
						// Launch rpc in-process
						lumex::rpc_config rpc_config{ config.node.network_params.network };
						error = lumex::read_rpc_config_toml (data_path, rpc_config, flags.rpc_config_overrides);
						if (error)
						{
							logger.critical (lumex::log::type::daemon, "Error deserializing RPC config: {}", error.get_message ());
							splash->hide ();
							show_error (error.get_message ());
							std::exit (1);
						}

						logger.debug (lumex::log::type::daemon, "Starting in-process RPC server on port {}", rpc_config.port);

						rpc_handler = std::make_unique<lumex::inprocess_rpc_handler> (*node, ipc, config.rpc);
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
				}
				QObject::connect (&application, &QApplication::aboutToQuit, [&] () {
					ipc.stop ();
					node->stop ();
					if (rpc)
					{
						rpc->stop ();
					}
#if USE_BOOST_PROCESS
					if (rpc_process)
					{
						rpc_process->terminate ();
					}
#endif
					runner.abort ();
				});
				QApplication::postEvent (&processor, new lumex_qt::eventloop_event ([&] () {
					gui = std::make_shared<lumex_qt::wallet> (application, processor, *node, wallet, wallet_config.account);
					splash->close ();
					gui->start ();
					gui->client_window->show ();
				}));
				result = QApplication::exec ();
				runner.join ();
			}
			catch (std::exception const & e)
			{
				splash->hide ();
				show_error ("Error initializing node: " + std::string (e.what ()));
			}
			write_wallet_config (wallet_config, data_path);
		}
		else
		{
			splash->hide ();
			show_error ("Error deserializing config: " + error.get_message ());
		}

		logger.info (lumex::log::type::daemon_wallet, "Daemon exiting (wallet)");

		return result;
	}
};
}

int main (int argc, char * const * argv)
{
	lumex::set_umask (); // Make sure the process umask is set before any files are created
	lumex::initialize_file_descriptor_limit ();
	lumex::logger::initialize (lumex::log_config::cli_default ());

	lumex::node_singleton_memory_pool_purge_guard memory_pool_cleanup_guard;

	QApplication application (argc, const_cast<char **> (argv));

	lumex::wallet_daemon daemon;

	try
	{
		boost::program_options::options_description description ("Command line options");
		// clang-format off
		description.add_options()
			("help", "Print out options")
			("config", boost::program_options::value<std::vector<lumex::config_key_value_pair>>()->multitoken(), "Pass configuration values. This takes precedence over any values in the node configuration file. This option can be repeated multiple times.")
			("rpcconfig", boost::program_options::value<std::vector<lumex::config_key_value_pair>>()->multitoken(), "Pass RPC configuration values. This takes precedence over any values in the RPC configuration file. This option can be repeated multiple times.");
		lumex::add_node_flag_options (description);
		lumex::add_node_options (description);
		// clang-format on
		boost::program_options::variables_map vm;
		try
		{
			boost::program_options::store (boost::program_options::parse_command_line (argc, argv, description), vm);
		}
		catch (boost::program_options::error const & err)
		{
			daemon.show_error (err.what ());
			return 1;
		}
		boost::program_options::notify (vm);
		int result (0);
		auto network (vm.find ("network"));
		if (network != vm.end ())
		{
			auto parsed = lumex::parse_network (network->second.as<std::string> ());
			if (!parsed)
			{
				daemon.show_error ("Invalid network. Valid values are live, test, beta and dev.");
				std::exit (1);
			}
			lumex::set_active_network (parsed.value ());
		}

		std::vector<std::string> config_overrides;
		const auto configItr = vm.find ("config");
		if (configItr != vm.cend ())
		{
			config_overrides = lumex::config_overrides (configItr->second.as<std::vector<lumex::config_key_value_pair>> ());
		}

		auto ec = lumex::handle_node_options (vm);
		if (ec == lumex::error_cli::unknown_command)
		{
			if (vm.count ("help") != 0)
			{
				std::ostringstream outstream;
				description.print (outstream);
				std::string helpstring = outstream.str ();
				daemon.show_help (helpstring);
				return 1;
			}
			else
			{
				try
				{
					std::filesystem::path data_path;
					if (vm.count ("data_path"))
					{
						auto name (vm["data_path"].as<std::string> ());
						data_path = std::filesystem::path (name);
					}
					else
					{
						data_path = lumex::working_path ();
					}
					lumex::node_flags flags;
					lumex::update_flags (flags, vm);
					result = daemon.run_wallet (application, argc, argv, data_path, flags);
				}
				catch (std::exception const & e)
				{
					daemon.show_error (boost::str (boost::format ("Exception while running wallet: %1%") % e.what ()));
				}
				catch (...)
				{
					daemon.show_error ("Unknown exception while running wallet");
				}
			}
		}
		return result;
	}
	catch (std::exception const & e)
	{
		daemon.show_error (boost::str (boost::format ("Exception while initializing %1%") % e.what ()));
	}
	catch (...)
	{
		daemon.show_error (boost::str (boost::format ("Unknown exception while initializing")));
	}
	return 1;
}
