#include <lumex/lib/enum_util.hpp>
#include <lumex/lib/stats_enums.hpp>
#include <lumex/node/bootstrap/common.hpp>

namespace lumex::bootstrap
{

lumex::stat::detail to_stat_detail (lumex::bootstrap::query_type type)
{
	return lumex::enum_convert<lumex::stat::detail> (type);
}

lumex::stat::detail to_stat_detail (lumex::bootstrap::query_source source)
{
	return lumex::enum_convert<lumex::stat::detail> (source);
}

}
