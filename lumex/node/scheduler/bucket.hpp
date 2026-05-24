#pragma once

#include <lumex/lib/errors.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/node/scheduler/priority_pool.hpp>
#include <lumex/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <set>

namespace mi = boost::multi_index;

namespace lumex::scheduler
{
class priority_config;

/**
 * A class which holds an ordered set of blocks to be scheduled, ordered by their block arrival time
 */
class bucket final
{
public:
	lumex::bucket_index const index;

public:
	bucket (lumex::bucket_index, priority_config const &, lumex::active_elections &, lumex::stats &, lumex::logger &);
	~bucket ();

	bool available (priority_entry top) const;
	bool activate (priority_entry top);
	bool cleanup ();

	size_t election_count () const;

private:
	bool activate_predicate (lumex::priority_timestamp candidate) const;
	bool overfill_predicate () const;
	bool cancel_lowest_election ();

private: // Dependencies
	priority_config const & config;
	lumex::active_elections & active;
	lumex::stats & stats;
	lumex::logger & logger;

private: // Elections
	struct election_entry
	{
		std::shared_ptr<lumex::election> election;
		lumex::qualified_root root;
		lumex::priority_timestamp priority;
	};

	// clang-format off
	class tag_sequenced {};
	class tag_root {};
	class tag_priority {};
	// clang-format on

	// clang-format off
	using ordered_elections = boost::multi_index_container<election_entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<election_entry, lumex::qualified_root, &election_entry::root>>,
		mi::ordered_non_unique<mi::tag<tag_priority>,
			mi::member<election_entry, lumex::priority_timestamp, &election_entry::priority>, std::greater<>> // Descending
	>>;
	// clang-format on

	ordered_elections elections;

private:
	mutable lumex::mutex mutex;
};
}
