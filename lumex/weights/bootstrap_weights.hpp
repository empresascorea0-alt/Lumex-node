#pragma once

#include <lumex/lib/fwd.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>

#include <boost/multiprecision/cpp_int.hpp>

#include <unordered_map>

/*
 * Bootstrap weights provide a hardcoded set of representative weights used during initial node synchronization.
 * These pre-configured weights allow the node to make informed decisions about which blocks to trust
 * until it has synchronized enough data to calculate actual representative weights from the ledger.
 */
namespace lumex
{
struct bootstrap_weights
{
	uint64_t max_blocks{ 0 };
	std::unordered_map<lumex::account, lumex::uint128_t> representatives;
};

bootstrap_weights get_bootstrap_weights (lumex::network_type);
}
