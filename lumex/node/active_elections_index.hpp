#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/secure/common.hpp>

#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>

namespace mi = boost::multi_index;

namespace lumex
{
class active_elections_index
{
public:
	struct entry
	{
		std::shared_ptr<lumex::election> election;

		lumex::qualified_root root;
		lumex::election_behavior behavior;
		lumex::bucket_index bucket;
		lumex::priority_timestamp priority;

		std::chrono::steady_clock::time_point timestamp{};
	};

public:
	void insert (std::shared_ptr<lumex::election> const &, lumex::election_behavior, lumex::bucket_index, lumex::priority_timestamp);
	bool erase (std::shared_ptr<lumex::election> const &);
	void update (std::shared_ptr<lumex::election> const &, lumex::election_behavior);

	bool exists (lumex::qualified_root const &) const;
	bool exists (std::shared_ptr<lumex::election> const &) const;

	std::shared_ptr<lumex::election> election (lumex::qualified_root const &) const;
	std::optional<entry> info (std::shared_ptr<lumex::election> const &) const;

	size_t size () const;
	size_t size (lumex::election_behavior) const;
	size_t size (lumex::election_behavior, lumex::bucket_index) const;
	bool empty () const;

	// Returns election with the highest priority value. NOTE: Lower "priority" is better
	using priority_result = std::pair<std::shared_ptr<lumex::election>, lumex::priority_timestamp>;
	priority_result last (lumex::election_behavior, lumex::bucket_index) const;

	std::deque<std::shared_ptr<lumex::election>> list () const;

	// Return list of elections with a timestamp before the specified cutoff time
	std::deque<std::shared_ptr<lumex::election>> list (
	std::chrono::steady_clock::time_point cutoff,
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now ());

	// Mark an election for update (reset its timestamp)
	bool trigger (std::shared_ptr<lumex::election> const &);

	// Are there any elections with a timestamp before the specified cutoff time
	bool any (std::chrono::steady_clock::time_point cutoff) const;

	void clear ();

private:
	// clang-format off
	class tag_sequenced {};
	class tag_root {};
	class tag_ptr {};
	class tag_key {};
	class tag_timestamp {};

	using ordered_entries = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<entry, lumex::qualified_root, &entry::root>>,
		mi::hashed_unique<mi::tag<tag_ptr>,
		    mi::member<entry, std::shared_ptr<lumex::election>, &entry::election>>,
		mi::ordered_non_unique<mi::tag<tag_key>,
			mi::composite_key<entry,
				mi::member<entry, lumex::election_behavior, &entry::behavior>,
				mi::member<entry, lumex::bucket_index, &entry::bucket>,
				mi::member<entry, lumex::priority_timestamp, &entry::priority>>>,
		mi::ordered_non_unique<mi::tag<tag_timestamp>,
			mi::member<entry, std::chrono::steady_clock::time_point, &entry::timestamp>>
	>>;
	// clang-format on
	ordered_entries entries;

	// Keep track of the total number of elections to provide constant time lookups
	using behavior_key_t = lumex::election_behavior;
	std::map<behavior_key_t, size_t> size_by_behavior;

	using bucket_key_t = std::pair<lumex::election_behavior, lumex::bucket_index>;
	std::map<bucket_key_t, size_t> size_by_bucket;
};
}
