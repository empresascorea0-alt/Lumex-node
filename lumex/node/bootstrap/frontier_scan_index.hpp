#pragma once

#include <lumex/node/bootstrap/bootstrap_config.hpp>
#include <lumex/node/bootstrap/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <map>
#include <set>

namespace mi = boost::multi_index;

namespace lumex::bootstrap
{
/*
 * Frontier scan divides the account space into ranges and scans each range for outdated frontiers in parallel.
 * This class is used to track the progress of each range.
 */
class frontier_scan_index
{
public:
	frontier_scan_index (frontier_scan_config const &, lumex::stats &);

	lumex::account next ();
	bool process (lumex::account start, std::deque<std::pair<lumex::account, lumex::block_hash>> const & response);

	void reset ();

	lumex::container_info container_info () const;

private: // Dependencies
	frontier_scan_config const & config;
	lumex::stats & stats;

private:
	// Represents a range of accounts to scan, once the full range is scanned (goes past `end`) the head wraps around (to the `start`)
	struct frontier_head
	{
		frontier_head (lumex::account start_a, lumex::account end_a) :
			start{ start_a },
			end{ end_a },
			next{ start_a }
		{
		}

		// The range of accounts to scan is [start, end)
		lumex::account const start;
		lumex::account const end;

		// We scan the range by querying frontiers starting at 'next' and gathering candidates
		lumex::account next;
		std::set<lumex::account> candidates;

		unsigned requests{ 0 };
		unsigned completed{ 0 };
		std::chrono::steady_clock::time_point timestamp{};
		size_t processed{ 0 }; // Total number of accounts processed

		lumex::account index () const
		{
			return start;
		}

		void reset ()
		{
			next = start;
			candidates.clear ();
			requests = 0;
			completed = 0;
			timestamp = {};
			processed = 0;
		}
	};

	// clang-format off
	class tag_sequenced {};
	class tag_start {};
	class tag_timestamp {};

	using ordered_heads = boost::multi_index_container<frontier_head,
	mi::indexed_by<
		mi::random_access<mi::tag<tag_sequenced>>,
		mi::ordered_unique<mi::tag<tag_start>,
			mi::const_mem_fun<frontier_head, lumex::account, &frontier_head::index>>,
		mi::ordered_non_unique<mi::tag<tag_timestamp>,
			mi::member<frontier_head, std::chrono::steady_clock::time_point, &frontier_head::timestamp>>
	>>;
	// clang-format on

	ordered_heads heads;
};
}