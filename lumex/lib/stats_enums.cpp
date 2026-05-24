#include <lumex/lib/enum_util.hpp>
#include <lumex/lib/stats_enums.hpp>

std::string_view lumex::stat::to_string (lumex::stat::type type)
{
	return lumex::enum_to_string (type);
}

std::string_view lumex::stat::to_string (lumex::stat::detail detail)
{
	return lumex::enum_to_string (detail);
}

std::string_view lumex::stat::to_string (lumex::stat::dir dir)
{
	return lumex::enum_to_string (dir);
}

std::string_view lumex::stat::to_string (lumex::stat::sample sample)
{
	return lumex::enum_to_string (sample);
}
