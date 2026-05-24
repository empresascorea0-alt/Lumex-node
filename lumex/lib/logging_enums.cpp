#include <lumex/lib/enum_util.hpp>
#include <lumex/lib/logging_enums.hpp>
#include <lumex/lib/utility.hpp>

std::string_view lumex::log::to_string (lumex::log::type tag)
{
	return lumex::enum_to_string (tag);
}

std::string_view lumex::log::to_string (lumex::log::detail detail)
{
	return lumex::enum_to_string (detail);
}

std::string_view lumex::log::to_string (lumex::log::level level)
{
	return lumex::enum_to_string (level);
}

const std::vector<lumex::log::level> & lumex::log::all_levels ()
{
	return lumex::enum_values<lumex::log::level> ();
}

const std::vector<lumex::log::type> & lumex::log::all_types ()
{
	return lumex::enum_values<lumex::log::type> ();
}

lumex::log::level lumex::log::parse_level (std::string_view name)
{
	auto value = lumex::enum_try_parse<lumex::log::level> (name);
	if (value.has_value ())
	{
		return value.value ();
	}
	auto all_levels_str = lumex::util::join (lumex::log::all_levels (), ", ", [] (auto const & lvl) {
		return to_string (lvl);
	});
	throw std::invalid_argument ("Invalid log level: " + std::string (name) + ". Must be one of: " + all_levels_str);
}

lumex::log::type lumex::log::parse_type (std::string_view name)
{
	auto value = lumex::enum_try_parse<lumex::log::type> (name);
	if (value.has_value ())
	{
		return value.value ();
	}
	throw std::invalid_argument ("Invalid log type: " + std::string (name));
}

lumex::log::detail lumex::log::parse_detail (std::string_view name)
{
	auto value = lumex::enum_try_parse<lumex::log::detail> (name);
	if (value.has_value ())
	{
		return value.value ();
	}
	throw std::invalid_argument ("Invalid log detail: " + std::string (name));
}

std::string_view lumex::log::to_string (lumex::log::tracing_format format)
{
	return lumex::enum_to_string (format);
}

lumex::log::tracing_format lumex::log::parse_tracing_format (std::string_view name)
{
	auto value = lumex::enum_try_parse<lumex::log::tracing_format> (name);
	if (value.has_value ())
	{
		return value.value ();
	}
	auto all_formats_str = lumex::util::join (lumex::log::all_tracing_formats (), ", ", [] (auto const & fmt) {
		return to_string (fmt);
	});
	throw std::invalid_argument ("Invalid tracing format: " + std::string (name) + ". Must be one of: " + all_formats_str);
}

const std::vector<lumex::log::tracing_format> & lumex::log::all_tracing_formats ()
{
	return lumex::enum_values<lumex::log::tracing_format> ();
}