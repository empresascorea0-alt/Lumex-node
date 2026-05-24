#include <lumex/lib/config.hpp>
#include <lumex/lib/enum_util.hpp>
#include <lumex/lib/env.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/logging_enums.hpp>
#include <lumex/lib/utility.hpp>

#include <fmt/chrono.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>

lumex::logger & lumex::default_logger ()
{
	static lumex::logger logger{ "default" };
	return logger;
}

/*
 * logger
 */

bool lumex::logger::global_initialized{ false };
lumex::log_config lumex::logger::global_config{};
std::vector<spdlog::sink_ptr> lumex::logger::global_sinks{};
lumex::object_stream_config lumex::logger::global_tracing_config{};

// By default, use only the tag as the logger name, since only one node is running in the process
std::function<std::string (lumex::log::logger_id, std::string identifier)> lumex::logger::global_name_formatter{ [] (lumex::log::logger_id logger_id, std::string identifier) {
	return to_string (logger_id);
} };

void lumex::logger::initialize (lumex::log_config fallback, std::optional<std::filesystem::path> data_path, std::vector<std::string> const & config_overrides)
{
	// Only load log config from file if data_path is available (i.e. not running in cli mode)
	lumex::log_config config = data_path ? lumex::load_log_config (fallback, *data_path, config_overrides) : fallback;
	initialize_common (config, data_path);
	global_initialized = true;
}

// Custom log formatter flags
namespace
{
/// Takes a qualified identifier in the form `node_identifier::tag` and splits it into a pair of `identifier` and `tag`
/// It is a limitation of spldlog that we cannot attach additional data to the logger, so we have to encode the node identifier in the logger name
/// @returns <node identifier, tag>
std::pair<std::string_view, std::string_view> split_qualified_identifier (std::string_view qualified_identifier)
{
	auto pos = qualified_identifier.find ("::");
	debug_assert (pos != std::string_view::npos); // This should never happen, since the default logger name formatter always adds the tag
	if (pos == std::string_view::npos)
	{
		return { std::string_view{}, qualified_identifier };
	}
	else
	{
		return { qualified_identifier.substr (0, pos), qualified_identifier.substr (pos + 2) };
	}
}

class identifier_formatter_flag : public spdlog::custom_flag_formatter
{
public:
	void format (const spdlog::details::log_msg & msg, const std::tm & tm, spdlog::memory_buf_t & dest) override
	{
		// Extract identifier and tag from logger name
		auto [identifier, tag] = split_qualified_identifier (std::string_view (msg.logger_name.data (), msg.logger_name.size ()));
		dest.append (identifier.data (), identifier.data () + identifier.size ());
	}

	std::unique_ptr<custom_flag_formatter> clone () const override
	{
		return spdlog::details::make_unique<identifier_formatter_flag> ();
	}
};

class tag_formatter_flag : public spdlog::custom_flag_formatter
{
public:
	void format (const spdlog::details::log_msg & msg, const std::tm & tm, spdlog::memory_buf_t & dest) override
	{
		// Extract identifier and tag from logger name
		auto [identifier, tag] = split_qualified_identifier (std::string_view (msg.logger_name.data (), msg.logger_name.size ()));
		dest.append (tag.data (), tag.data () + tag.size ());
	}

	std::unique_ptr<custom_flag_formatter> clone () const override
	{
		return spdlog::details::make_unique<tag_formatter_flag> ();
	}
};
}

void lumex::logger::initialize_for_tests (lumex::log_config fallback)
{
	auto config = lumex::load_log_config (std::move (fallback), /* load log config from current workdir */ std::filesystem::current_path ());
	initialize_common (config, /* store log file in current workdir */ std::filesystem::current_path ());

	// Use tag and identifier as the logger name, since multiple nodes may be running in the same process
	global_name_formatter = [] (lumex::log::logger_id logger_id, std::string identifier) {
		return fmt::format ("{}::{}", identifier, to_string (logger_id));
	};

	// Setup formatter to include information about node identifier `[%i]` and tag `[%n]`
	auto formatter = std::make_unique<spdlog::pattern_formatter> ();
	formatter->add_flag<identifier_formatter_flag> ('i');
	formatter->add_flag<tag_formatter_flag> ('n');
	formatter->set_pattern ("[%Y-%m-%d %H:%M:%S.%e] [%i] [%n] [%l] %v");

	for (auto & sink : global_sinks)
	{
		sink->set_formatter (formatter->clone ());
	}

	global_initialized = true;
}

void lumex::logger::initialize_dummy ()
{
	initialize_common (lumex::log_config::dummy_default (), std::nullopt);
	global_initialized = true;
}

// Using std::cerr here, since logging may not be initialized yet
void lumex::logger::initialize_common (lumex::log_config const & config, std::optional<std::filesystem::path> data_path)
{
	global_config = config;

	spdlog::set_automatic_registration (false);
	spdlog::set_level (to_spdlog_level (config.default_level));

	global_sinks.clear ();

	// Console setup
	if (config.console.enable)
	{
		if (!config.console.to_cerr)
		{
			// Only use colors if not writing to cerr
			if (config.console.colors)
			{
				auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt> ();
				global_sinks.push_back (console_sink);
			}
			else
			{
				auto console_sink = std::make_shared<spdlog::sinks::stdout_sink_mt> ();
				global_sinks.push_back (console_sink);
			}
		}
		else
		{
			if (config.console.colors)
			{
				std::cerr << "WARNING: Logging to cerr is enabled, console colors will be disabled" << std::endl;
			}

			auto cerr_sink = std::make_shared<spdlog::sinks::stderr_sink_mt> ();
			global_sinks.push_back (cerr_sink);
		}
	}

	// File setup
	if (config.file.enable)
	{
		// In cases where data_path is not available, file logging should always be disabled
		release_assert (data_path);

		auto now = std::chrono::system_clock::now ();
		auto time = std::chrono::system_clock::to_time_t (now);

		auto filename = fmt::format ("log_{:%Y-%m-%d_%H-%M}-{:%S}", fmt::localtime (time), now.time_since_epoch ());
		std::replace (filename.begin (), filename.end (), '.', '_'); // Replace millisecond dot separator with underscore

		std::filesystem::path log_path{ data_path.value () / "log" / (filename + ".log") };
		log_path = std::filesystem::absolute (log_path);

		std::cerr << "Logging to file: " << log_path.string () << std::endl;

		// If either max_size or rotation_count is 0, then disable file rotation
		if (config.file.max_size == 0 || config.file.rotation_count == 0)
		{
			std::cerr << "WARNING: Log file rotation is disabled, log file size may grow without bound" << std::endl;

			auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt> (log_path.string (), true);
			global_sinks.push_back (file_sink);
		}
		else
		{
			auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt> (log_path.string (), config.file.max_size, config.file.rotation_count);
			global_sinks.push_back (file_sink);
		}
	}

	// Tracing setup
	switch (config.tracing_format)
	{
		case lumex::log::tracing_format::standard:
			global_tracing_config = lumex::object_stream_config::default_config ();
			break;
		case lumex::log::tracing_format::json:
			global_tracing_config = lumex::object_stream_config::json_config ();
			break;
	}
}

void lumex::logger::flush ()
{
	for (auto & sink : global_sinks)
	{
		sink->flush ();
	}
}

/*
 * logger
 */

lumex::logger::logger (std::string identifier) :
	identifier{ std::move (identifier) }
{
	if (!global_initialized)
	{
		throw std::runtime_error{ "logging should be initialized before creating a logger" };
	}
}

lumex::logger::~logger ()
{
	flush ();
}

spdlog::logger & lumex::logger::get_logger (lumex::log::type type, lumex::log::detail detail)
{
	// This is a two-step process to avoid exclusively locking the mutex in the common case
	{
		std::shared_lock lock{ mutex };

		if (auto it = spd_loggers.find ({ type, detail }); it != spd_loggers.end ())
		{
			return *it->second;
		}
	}
	// Not found, create a new logger
	{
		std::unique_lock lock{ mutex };

		auto [it, inserted] = spd_loggers.emplace (std::make_pair (type, detail), make_logger ({ type, detail }));
		return *it->second;
	}
}

std::shared_ptr<spdlog::logger> lumex::logger::make_logger (lumex::log::logger_id logger_id)
{
	auto const & config = global_config;
	auto const & sinks = global_sinks;

	auto name = global_name_formatter (logger_id, identifier);
	auto spd_logger = std::make_shared<spdlog::logger> (name, sinks.begin (), sinks.end ());

	spd_logger->set_level (to_spdlog_level (find_level (logger_id)));
	spd_logger->flush_on (to_spdlog_level (config.flush_level));

	return spd_logger;
}

lumex::log::level lumex::logger::find_level (lumex::log::logger_id logger_id) const
{
	auto const & config = global_config;
	auto const & [type, detail] = logger_id;

	// Check for a specific level for this logger
	if (auto it = config.levels.find (logger_id); it != config.levels.end ())
	{
		return it->second;
	}
	// Check for a default level for this logger type
	if (auto it = config.levels.find ({ type, lumex::log::detail::all }); it != config.levels.end ())
	{
		return it->second;
	}
	// Use the default level
	return config.default_level;
}

spdlog::level::level_enum lumex::logger::to_spdlog_level (lumex::log::level level)
{
	switch (level)
	{
		case lumex::log::level::off:
			return spdlog::level::off;
		case lumex::log::level::critical:
			return spdlog::level::critical;
		case lumex::log::level::error:
			return spdlog::level::err;
		case lumex::log::level::warn:
			return spdlog::level::warn;
		case lumex::log::level::info:
			return spdlog::level::info;
		case lumex::log::level::debug:
			return spdlog::level::debug;
		case lumex::log::level::trace:
			return spdlog::level::trace;
	}
	debug_assert (false, "Invalid log level");
	return spdlog::level::off;
}

/*
 * logging config presets
 */

lumex::log_config lumex::log_config::cli_default (lumex::log::level default_level)
{
	log_config config{};
	config.default_level = default_level;
	config.console.colors = false;
	config.console.to_cerr = true; // Use cerr to avoid interference with CLI output that goes to stdout
	config.file.enable = false;
	return config;
}

lumex::log_config lumex::log_config::daemon_default ()
{
	log_config config{};
	config.default_level = lumex::log::level::info;
	return config;
}

lumex::log_config lumex::log_config::tests_default ()
{
	log_config config{};
	config.default_level = lumex::log::level::off;
	config.file.enable = false;
	return config;
}

lumex::log_config lumex::log_config::dummy_default ()
{
	log_config config{};
	config.default_level = lumex::log::level::off;
	config.flush_level = lumex::log::level::off;
	config.console.enable = false;
	config.file.enable = false;
	return config;
}

lumex::log_config lumex::log_config::sample_config ()
{
	log_config config{};
	config.default_level = lumex::log::level::info;
	config.levels = default_levels (lumex::log::level::info); // Populate with default levels
	return config;
}

/*
 * logging config
 */

lumex::error lumex::log_config::serialize_toml (lumex::tomlconfig & toml) const
{
	lumex::tomlconfig config_toml;
	serialize (config_toml);
	toml.put_child ("log", config_toml);

	return toml.get_error ();
}

lumex::error lumex::log_config::deserialize_toml (lumex::tomlconfig & toml)
{
	try
	{
		auto logging_l = toml.get_optional_child ("log");
		if (logging_l)
		{
			deserialize (*logging_l);
		}
	}
	catch (std::invalid_argument const & ex)
	{
		toml.get_error ().set (ex.what ());
	}

	return toml.get_error ();
}

void lumex::log_config::serialize (lumex::tomlconfig & toml) const
{
	toml.put ("default_level", std::string{ to_string (default_level) });

	lumex::tomlconfig console_config;
	console_config.put ("enable", console.enable);
	console_config.put ("to_cerr", console.to_cerr);
	console_config.put ("colors", console.colors);
	toml.put_child ("console", console_config);

	lumex::tomlconfig file_config;
	file_config.put ("enable", file.enable);
	file_config.put ("max_size", file.max_size);
	file_config.put ("rotation_count", file.rotation_count);
	toml.put_child ("file", file_config);

	lumex::tomlconfig levels_config;
	for (auto const & [logger_id, level] : levels)
	{
		auto logger_name = to_string (logger_id.first);
		levels_config.put (std::string{ logger_name }, std::string{ to_string (level) });
	}
	toml.put_child ("levels", levels_config);
}

void lumex::log_config::deserialize (lumex::tomlconfig & toml)
{
	if (toml.has_key ("default_level"))
	{
		auto default_level_l = toml.get<std::string> ("default_level");
		default_level = lumex::log::parse_level (default_level_l);
	}

	if (toml.has_key ("console"))
	{
		auto console_config = toml.get_required_child ("console");
		console_config.get ("enable", console.enable);
		console_config.get ("to_cerr", console.to_cerr);
		console_config.get ("colors", console.colors);
	}

	if (toml.has_key ("file"))
	{
		auto file_config = toml.get_required_child ("file");
		file_config.get ("enable", file.enable);
		file_config.get ("max_size", file.max_size);
		file_config.get ("rotation_count", file.rotation_count);
	}

	if (toml.has_key ("levels"))
	{
		auto levels_config = toml.get_required_child ("levels");
		for (auto & level : levels_config.get_values<std::string> ())
		{
			try
			{
				auto & [name_str, level_str] = level;
				auto logger_level = lumex::log::parse_level (level_str);
				auto logger_id = lumex::log::parse_logger_id (name_str);

				levels[logger_id] = logger_level;
			}
			catch (std::invalid_argument const & ex)
			{
				// Ignore but warn about invalid logger names
				std::cerr << "Problem processing log config: " << ex.what () << std::endl;
			}
		}
	}
}

std::map<lumex::log::logger_id, lumex::log::level> lumex::log_config::default_levels (lumex::log::level default_level)
{
	std::map<lumex::log::logger_id, lumex::log::level> result;
	for (auto const & type : lumex::log::all_types ())
	{
		result.emplace (std::make_pair (type, lumex::log::detail::all), default_level);
	}
	return result;
}

/*
 * config loading
 */

// Using std::cerr here, since logging may not be initialized yet
lumex::log_config lumex::load_log_config (lumex::log_config fallback, const std::filesystem::path & data_path, const std::vector<std::string> & config_overrides)
{
	try
	{
		auto config = lumex::load_config_file<lumex::log_config> (fallback, log_config_filename, data_path, config_overrides);

		// Parse default log level from environment variable, e.g. "LUMEX_LOG=debug"
		auto env_level = lumex::env::get ("LUMEX_LOG");
		if (env_level)
		{
			try
			{
				auto level = lumex::log::parse_level (*env_level);
				config.default_level = level;

				std::cerr << "Using default log level from LUMEX_LOG environment variable: " << to_string (level) << std::endl;
			}
			catch (std::invalid_argument const & ex)
			{
				std::cerr << "Invalid log level from LUMEX_LOG environment variable: " << ex.what () << std::endl;
			}
		}

		// Parse per logger levels from environment variable, e.g. "LUMEX_LOG_LEVELS=ledger=debug,node=trace"
		if (auto env_levels = lumex::env::get ("LUMEX_LOG_LEVELS"))
		{
			std::map<lumex::log::logger_id, lumex::log::level> levels;
			for (auto const & env_level_str : lumex::util::split (*env_levels, ","))
			{
				try
				{
					// Split 'logger_name=level' into a pair of 'logger_name' and 'level'
					auto arr = lumex::util::split (env_level_str, "=");
					if (arr.size () != 2)
					{
						throw std::invalid_argument ("Invalid entry: " + env_level_str);
					}

					auto name_str = arr[0];
					auto level_str = arr[1];

					auto logger_id = lumex::log::parse_logger_id (name_str);
					auto logger_level = lumex::log::parse_level (level_str);

					levels[logger_id] = logger_level;

					std::cerr << "Using logger log level from LUMEX_LOG_LEVELS environment variable: " << to_string (logger_id) << "=" << to_string (logger_level) << std::endl;
				}
				catch (std::invalid_argument const & ex)
				{
					std::cerr << "Invalid log level from LUMEX_LOG_LEVELS environment variable: " << ex.what () << std::endl;
				}
			}

			// Merge with existing levels
			for (auto const & [logger_id, level] : levels)
			{
				config.levels[logger_id] = level;
			}
		}

		if (auto env_tracing_format = lumex::env::get ("LUMEX_TRACE_FORMAT"))
		{
			try
			{
				auto tracing_format = lumex::log::parse_tracing_format (*env_tracing_format);
				config.tracing_format = tracing_format;

				std::cerr << "Using trace format from LUMEX_TRACE_FORMAT environment variable: " << to_string (tracing_format) << std::endl;
			}
			catch (std::invalid_argument const & ex)
			{
				std::cerr << "Invalid trace format from LUMEX_TRACE_FORMAT environment variable: " << ex.what () << std::endl;
			}
		}

		auto tracing_configured = [&] () {
			if (config.default_level == lumex::log::level::trace)
			{
				return true;
			}
			for (auto const & [logger_id, level] : config.levels)
			{
				if (level == lumex::log::level::trace)
				{
					return true;
				}
			}
			return false;
		};

		if (tracing_configured () && !is_tracing_enabled ())
		{
			std::cerr << "WARNING: Tracing is not enabled in this build, but log level is set to trace" << std::endl;
		}

		return config;
	}
	catch (std::runtime_error const & ex)
	{
		std::cerr << "Unable to load log config. Using defaults. Error: " << ex.what () << std::endl;
	}
	return fallback;
}

std::string lumex::log::to_string (lumex::log::logger_id logger_id)
{
	auto const & [type, detail] = logger_id;
	if (detail == lumex::log::detail::all)
	{
		return fmt::format ("{}", to_string (type));
	}
	else
	{
		return fmt::format ("{}::{}", to_string (type), to_string (detail));
	}
}

/**
 * Parse `logger_name[:logger_detail]` into a pair of `log::type` and `log::detail`
 * @throw std::invalid_argument if `logger_name` or `logger_detail` are invalid
 */
lumex::log::logger_id lumex::log::parse_logger_id (const std::string & logger_name)
{
	auto parts = lumex::util::split (logger_name, "::");
	if (parts.size () == 1)
	{
		return { lumex::log::parse_type (parts[0]), lumex::log::detail::all };
	}
	if (parts.size () == 2)
	{
		return { lumex::log::parse_type (parts[0]), lumex::log::parse_detail (parts[1]) };
	}
	throw std::invalid_argument ("Invalid logger name: " + logger_name);
}