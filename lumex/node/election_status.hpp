#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/lib/stats_enums.hpp>

#include <chrono>
#include <memory>

namespace lumex
{
class block;
}

namespace lumex
{
/* Defines the possible states for an election to stop in */
enum class election_status_type : uint8_t
{
	ongoing = 0,
	active_confirmed_quorum = 1,
	active_confirmation_height = 2,
	inactive_confirmation_height = 3,
	stopped = 5
};

std::string_view to_string (election_status_type);
lumex::stat::detail to_stat_detail (election_status_type);

/* Holds a summary of an election */
class election_status final
{
public:
	std::shared_ptr<lumex::block> winner;
	lumex::amount tally{ 0 };
	lumex::amount final_tally{ 0 };
	std::chrono::system_clock::time_point election_end{};
	std::chrono::milliseconds election_duration{};
	unsigned confirmation_request_count{ 0 };
	unsigned vote_broadcast_count{ 0 };
	unsigned block_count{ 0 };
	unsigned voter_count{ 0 };
	election_status_type type{ lumex::election_status_type::inactive_confirmation_height };

	election_status () = default;

	election_status (std::shared_ptr<lumex::block> block_a, election_status_type type_a = lumex::election_status_type::ongoing) :
		winner (block_a),
		type (type_a)
	{
		block_count = 1;
	}
};
}
