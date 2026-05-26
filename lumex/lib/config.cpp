#include <lumex/crypto/blake2/blake2.h>
#include <lumex/lib/block_type.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/config.hpp>
#include <lumex/lib/constants.hpp>
#include <lumex/lib/env.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/version.hpp>

#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/numeric/conversion/cast.hpp>

#include <valgrind/valgrind.h>

namespace lumex
{
void force_lumex_dev_network ()
{
	lumex::set_active_network (lumex::network_type::lumex_dev_network);
}

bool is_dev_run ()
{
	return lumex::get_active_network () == lumex::network_type::lumex_dev_network;
}

bool running_within_valgrind ()
{
	return (RUNNING_ON_VALGRIND > 0);
}

bool memory_intensive_instrumentation ()
{
	auto env = lumex::env::get<bool> ("LUMEX_MEMORY_INTENSIVE");
	if (env)
	{
		return env.value ();
	}
	return is_tsan_build () || lumex::running_within_valgrind ();
}

bool slow_instrumentation ()
{
	return is_tsan_build () || lumex::running_within_valgrind ();
}

std::string get_node_toml_config_path (std::filesystem::path const & data_path)
{
	return (data_path / node_config_filename).string ();
}

std::string get_rpc_toml_config_path (std::filesystem::path const & data_path)
{
	return (data_path / rpc_config_filename).string ();
}

std::string get_qtwallet_toml_config_path (std::filesystem::path const & data_path)
{
	return (data_path / qtwallet_config_filename).string ();
}

std::string get_access_toml_config_path (std::filesystem::path const & data_path)
{
	return (data_path / access_config_filename).string ();
}

std::string get_tls_toml_config_path (std::filesystem::path const & data_path)
{
	return (data_path / tls_config_filename).string ();
}
}

uint16_t lumex::test_node_port ()
{
	static auto const test_env = [] () -> std::optional<uint16_t> {
		if (auto value = lumex::env::get<uint16_t> ("LUMEX_TEST_NODE_PORT"))
		{
			std::cerr << "Node port overridden by LUMEX_TEST_NODE_PORT environment variable: " << *value << std::endl;
			return *value;
		}
		return std::nullopt;
	}();
	return test_env.value_or (17075);
}

uint16_t lumex::test_rpc_port ()
{
	static auto const test_env = [] () -> std::optional<uint16_t> {
		if (auto value = lumex::env::get<uint16_t> ("LUMEX_TEST_RPC_PORT"))
		{
			std::cerr << "RPC port overridden by LUMEX_TEST_RPC_PORT environment variable: " << *value << std::endl;
			return *value;
		}
		return std::nullopt;
	}();
	return test_env.value_or (17076);
}

uint16_t lumex::test_ipc_port ()
{
	static auto const test_env = [] () -> std::optional<uint16_t> {
		if (auto value = lumex::env::get<uint16_t> ("LUMEX_TEST_IPC_PORT"))
		{
			std::cerr << "IPC port overridden by LUMEX_TEST_IPC_PORT environment variable: " << *value << std::endl;
			return *value;
		}
		return std::nullopt;
	}();
	return test_env.value_or (17077);
}

uint16_t lumex::test_websocket_port ()
{
	static auto const test_env = [] () -> std::optional<uint16_t> {
		if (auto value = lumex::env::get<uint16_t> ("LUMEX_TEST_WEBSOCKET_PORT"))
		{
			std::cerr << "Websocket port overridden by LUMEX_TEST_WEBSOCKET_PORT environment variable: " << *value << std::endl;
			return *value;
		}
		return std::nullopt;
	}();
	return test_env.value_or (17078);
}

uint32_t lumex::test_scan_wallet_reps_delay ()
{
	static auto const test_env = [] () -> std::optional<uint32_t> {
		if (auto value = lumex::env::get<uint32_t> ("LUMEX_TEST_WALLET_SCAN_REPS_DELAY"))
		{
			std::cerr << "Wallet scan interval overridden by LUMEX_TEST_WALLET_SCAN_REPS_DELAY environment variable: " << *value << std::endl;
			return *value;
		}
		return std::nullopt;
	}();
	return test_env.value_or (900000); // 15 minutes default
}

std::array<uint8_t, 2> lumex::test_magic_number ()
{
	static auto const test_env = [] () -> std::optional<std::string> {
		if (auto value = lumex::env::get<std::string> ("LUMEX_TEST_MAGIC_NUMBER"))
		{
			std::cerr << "Magic number overridden by LUMEX_TEST_MAGIC_NUMBER environment variable: " << *value << std::endl;
			return *value;
		}
		return std::nullopt;
	}();

	auto value = test_env.value_or ("LX");
	release_assert (value.size () == 2);
	std::array<uint8_t, 2> ret{};
	std::copy (value.begin (), value.end (), ret.data ());
	return ret;
}

size_t lumex::queue_warning_threshold ()
{
	static auto const env_override = [] () -> std::optional<size_t> {
		if (auto value = lumex::env::get<size_t> ("LUMEX_QUEUE_WARNING_THRESHOLD"))
		{
			std::cerr << "Queue warning threshold overridden by LUMEX_QUEUE_WARNING_THRESHOLD environment variable: " << *value << std::endl;
			return *value;
		}
		return std::nullopt;
	}();
	return env_override.value_or (100);
}

size_t lumex::ledger_thread_stack_size ()
{
	static auto const env_override = [] () -> std::optional<size_t> {
		if (auto value = lumex::env::get<size_t> ("LUMEX_LEDGER_THREAD_STACK_SIZE"))
		{
			std::cerr << "Ledger thread stack size overridden by LUMEX_LEDGER_THREAD_STACK_SIZE environment variable: " << *value << std::endl;
			return *value;
		}
		return std::nullopt;
	}();
	return env_override.value_or (128 * 1024 * 1024);
}

size_t lumex::ledger_max_rollback_depth ()
{
	static auto const env_override = [] () -> std::optional<size_t> {
		if (auto value = lumex::env::get<size_t> ("LUMEX_MAX_ROLLBACK_DEPTH"))
		{
			std::cerr << "Ledger max rollback depth overridden by LUMEX_MAX_ROLLBACK_DEPTH environment variable: " << *value << std::endl;
			return *value;
		}
		return std::nullopt;
	}();
	return env_override.value_or (100000);
}

lumex::database_backend lumex::default_database_backend ()
{
	static auto const env_override = [] () -> std::optional<lumex::database_backend> {
		if (auto value = lumex::env::get<std::string> ("LUMEX_BACKEND"))
		{
			auto backend = parse_database_backend (*value);
			if (backend.has_value ())
			{
				std::cerr << "Default database backend overridden by LUMEX_BACKEND environment variable: " << to_string (*backend) << std::endl;
				return *backend;
			}
			else
			{
				std::cerr << "Unknown database backend in LUMEX_BACKEND environment variable: " << *value << std::endl;
			}
		}
		return std::nullopt;
	}();
	return env_override.value_or (lumex::database_backend::lmdb);
}

/*
 *
 */

// Using std::cerr here, since logging may not be initialized yet
lumex::tomlconfig lumex::load_toml_file (const std::filesystem::path & config_filename, const std::filesystem::path & data_path, const std::vector<std::string> & config_overrides)
{
	std::stringstream config_overrides_stream;
	for (auto const & entry : config_overrides)
	{
		config_overrides_stream << entry << std::endl;
	}
	config_overrides_stream << std::endl;

	// Make sure we don't create an empty toml file if it doesn't exist. Running without a toml file is the default.
	auto toml_config_path = data_path / config_filename;
	if (std::filesystem::exists (toml_config_path))
	{
		lumex::tomlconfig toml;
		auto error = toml.read (config_overrides_stream, toml_config_path);
		if (error)
		{
			throw std::runtime_error (error.get_message ());
		}
		std::cerr << "Config file `" << config_filename.string () << "` loaded from node data directory: " << toml_config_path.string () << std::endl;
		return toml;
	}
	else
	{
		// If no config was found, return an empty config with overrides applied
		lumex::tomlconfig toml;
		auto error = toml.read (config_overrides_stream);
		if (error)
		{
			throw std::runtime_error (error.get_message ());
		}
		std::cerr << "Config file `" << config_filename.string () << "` not found, using default configuration" << std::endl;
		return toml;
	}
}
