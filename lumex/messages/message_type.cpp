#include <lumex/lib/enum_util.hpp>
#include <lumex/lib/logging_enums.hpp>
#include <lumex/lib/stats_enums.hpp>
#include <lumex/messages/message_type.hpp>

namespace lumex::messages
{
std::string_view to_string (message_type type)
{
	return lumex::enum_to_string (type);
}

lumex::stat::detail to_stat_detail (message_type type)
{
	return lumex::enum_convert<lumex::stat::detail> (type);
}

lumex::log::detail to_log_detail (message_type type)
{
	return lumex::enum_convert<lumex::log::detail> (type);
}
}
