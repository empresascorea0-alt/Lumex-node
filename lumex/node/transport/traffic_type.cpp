#include <lumex/lib/enum_util.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/node/transport/traffic_type.hpp>

#include <magic_enum.hpp>

std::string_view lumex::transport::to_string (lumex::transport::traffic_type type)
{
	return lumex::enum_to_string (type);
}

std::vector<lumex::transport::traffic_type> lumex::transport::all_traffic_types ()
{
	return lumex::enum_values<lumex::transport::traffic_type> ();
}

lumex::stat::detail lumex::transport::to_stat_detail (lumex::transport::traffic_type type)
{
	return lumex::enum_convert<lumex::stat::detail> (type);
}