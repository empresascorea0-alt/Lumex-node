#pragma once

#include <lumex/lib/fwd.hpp>

#include <string_view>

namespace lumex
{
enum class block_source
{
	unknown = 0,
	live,
	live_originator,
	bootstrap,
	bootstrap_legacy,
	unchecked,
	local,
	forced,
	election,
	test,
};

std::string_view to_string (block_source);
lumex::stat::detail to_stat_detail (block_source);
}