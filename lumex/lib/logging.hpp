#pragma once

#include <lumex/lib/formatting.hpp>
#include <lumex/lib/logging_enums.hpp>
#include <lumex/lib/logging_utils.hpp>
#include <lumex/lib/object_stream.hpp>
#include <lumex/lib/object_stream_adapters.hpp>
#include <lumex/lib/tomlconfig.hpp>

#include <initializer_list>
#include <memory>
#include <shared_mutex>
#include <sstream>

#include <fmt/ostream.h>
#include <spdlog/spdlog.h>

namespace lumex::log
{
template <class T>
struct arg
{
	std::string_view name;
	T const & value;

	arg (std::string_view name_a, T const & value_a) :
		name{ name_a },
		value{ value_a }
	{
	}
};

using logger_id = std::pair<lumex::log::type, lumex::log::detail>;

std::string to_string (logger_id);
logger_id parse_logger_id (std::string const &);
}

namespace lumex
{
consteval bool is_tracing_enabled ()
{
#ifdef LUMEX_TRACING
	return true;
#else
	return false;
#endif
}

class log_config final
{
public:
	lumex::error serialize_toml (lumex::tomlconfig &) const;
	lumex::error deserialize_toml (lumex::tomlconfig &);

private:
	void serialize (lumex::tomlconfig &) const;
	void deserialize (lumex::tomlconfig &);

public:
	lumex::log::level default_level{ lumex::log::level::info };
	lumex::log::level flush_level{ lumex::log::level::error };

	std::map<lumex::log::logger_id, lumex::log::level> levels;

	struct console_config
	{
		bool enable{ true };
		bool colors{ true };
		bool to_cerr{ false };
	};

	struct file_config
	{
		bool enable{ true };
		std::size_t max_size{ 32 * 1024 * 1024 };
		std::size_t rotation_count{ 4 };
	};

	console_config console;
	file_config file;

	lumex::log::tracing_format tracing_format{ lumex::log::tracing_format::standard };

public: // Predefined defaults
	static log_config cli_default (lumex::log::level default_level = lumex::log::level::critical);
	static log_config daemon_default ();
	static log_config tests_default ();
	static log_config dummy_default (); // For empty logger
	static log_config sample_config (); // For auto-generated sample config files

private:
	/// Returns placeholder log levels for all loggers
	static std::map<lumex::log::logger_id, lumex::log::level> default_levels (lumex::log::level);
};

lumex::log_config load_log_config (lumex::log_config fallback, std::filesystem::path const & data_path, std::vector<std::string> const & config_overrides = {});

class logger final
{
public:
	explicit logger (std::string identifier = "");
	~logger ();

	// Disallow copies
	logger (logger const &) = delete;

public:
	static void initialize (lumex::log_config fallback, std::optional<std::filesystem::path> data_path = std::nullopt, std::vector<std::string> const & config_overrides = {});
	static void initialize_for_tests (lumex::log_config fallback);
	static void initialize_dummy (); // TODO: This is less than ideal, provide `lumex::dummy_logger ()` instead
	static void flush ();

private:
	static bool global_initialized;
	static lumex::log_config global_config;
	static std::vector<spdlog::sink_ptr> global_sinks;
	static std::function<std::string (lumex::log::logger_id, std::string identifier)> global_name_formatter;
	static lumex::object_stream_config global_tracing_config;

	static void initialize_common (lumex::log_config const &, std::optional<std::filesystem::path> data_path);

public:
	template <class... Args>
	void log (lumex::log::level level, lumex::log::type type, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (type).log (to_spdlog_level (level), fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void debug (lumex::log::type type, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (type).debug (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void info (lumex::log::type type, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (type).info (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void warn (lumex::log::type type, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (type).warn (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void error (lumex::log::type type, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (type).error (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void critical (lumex::log::type type, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (type).critical (fmt, std::forward<Args> (args)...);
	}

public:
	template <typename... Args>
	void trace (lumex::log::type type, lumex::log::detail detail, Args &&... args)
	{
		if constexpr (is_tracing_enabled ())
		{
			debug_assert (detail != lumex::log::detail::all);

			// Include info about precise time of the event
			auto now = std::chrono::high_resolution_clock::now ();

			// TODO: Improve code indentation config
			auto & logger = get_logger (type, detail);
			logger.trace ("{}",
			lumex::streamed_args (global_tracing_config,
			lumex::log::arg{ "event", event_formatter{ type, detail } },
			lumex::log::arg{ "time", lumex::log::microseconds (now) },
			std::forward<Args> (args)...));
		}
	}

private:
	struct event_formatter final
	{
		lumex::log::type type;
		lumex::log::detail detail;

		friend std::ostream & operator<< (std::ostream & os, event_formatter const & self)
		{
			return os << to_string (self.type) << "::" << to_string (self.detail);
		}
	};

private:
	const std::string identifier;

	std::map<lumex::log::logger_id, std::shared_ptr<spdlog::logger>> spd_loggers;
	std::shared_mutex mutex;

private:
	spdlog::logger & get_logger (lumex::log::type, lumex::log::detail = lumex::log::detail::all);
	std::shared_ptr<spdlog::logger> make_logger (lumex::log::logger_id);
	lumex::log::level find_level (lumex::log::logger_id) const;

	static spdlog::level::level_enum to_spdlog_level (lumex::log::level);
};

/**
 * Returns a logger instance that can be used before node specific logging is available.
 * Should only be used for logging that happens during startup and initialization, since it won't contain node specific identifier.
 */
lumex::logger & default_logger ();
}