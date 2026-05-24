#pragma once

#include <lumex/lib/stats.hpp>

#include <string_view>

namespace lumex
{
enum class election_behavior
{
	manual,
	priority,
	/**
	 * Hinted elections:
	 * - shorter timespan
	 * - limited space inside AEC
	 */
	hinted,
	/**
	 * Optimistic elections:
	 * - shorter timespan
	 * - limited space inside AEC
	 * - more frequent confirmation requests
	 */
	optimistic,
};

std::string_view to_string (election_behavior);
lumex::stat::detail to_stat_detail (election_behavior);
}
