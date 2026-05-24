#pragma once

#include <lumex/lib/numbers.hpp>

#include <boost/multiprecision/cpp_int.hpp>

#include <chrono>

namespace lumex
{
class vote_with_weight_info final
{
public:
	lumex::account representative;
	std::chrono::steady_clock::time_point time;
	uint64_t timestamp;
	lumex::block_hash hash;
	lumex::uint128_t weight;
};
}
