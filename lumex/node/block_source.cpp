#include <lumex/lib/enum_util.hpp>
#include <lumex/lib/stats_enums.hpp>
#include <lumex/node/block_source.hpp>

std::string_view lumex::to_string (lumex::block_source source)
{
	return lumex::enum_to_string (source);
}

lumex::stat::detail lumex::to_stat_detail (lumex::block_source type)
{
	return lumex::enum_convert<lumex::stat::detail> (type);
}