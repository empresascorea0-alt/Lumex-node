#pragma once

#include <lumex/crypto_lib/random_pool.hpp>
#include <lumex/lib/stats_enums.hpp>

namespace lumex::bootstrap
{
using id_t = uint64_t;

static lumex::bootstrap::id_t generate_id ()
{
	lumex::bootstrap::id_t id;
	lumex::random_pool::generate_block (reinterpret_cast<uint8_t *> (&id), sizeof (id));
	return id;
}

enum class query_type
{
	invalid = 0,
	blocks_by_hash,
	blocks_by_account,
	account_info_by_hash,
	frontiers,
};

enum class query_source
{
	invalid = 0,
	priority,
	database,
	dependencies,
	frontiers,
};

lumex::stat::detail to_stat_detail (lumex::bootstrap::query_type);
lumex::stat::detail to_stat_detail (lumex::bootstrap::query_source);
}
