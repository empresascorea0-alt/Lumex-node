#pragma once

#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/node/election_status.hpp>
#include <lumex/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

namespace mi = boost::multi_index;

namespace lumex
{
class recently_confirmed_cache final
{
public:
	explicit recently_confirmed_cache (std::size_t max_size);

	void put (lumex::qualified_root const &, lumex::block_hash const &, lumex::election_status const &);
	void erase (lumex::block_hash const &);
	void clear ();
	std::size_t size () const;

	bool contains (lumex::qualified_root const &) const;
	bool contains (lumex::block_hash const &) const;

	struct latency_stats
	{
		uint32_t p50{ 0 };
		uint32_t p90{ 0 };
		uint32_t p99{ 0 };
	};

	latency_stats latency_percentiles () const;

	lumex::container_info container_info () const;

private:
	struct entry
	{
		lumex::qualified_root root;
		lumex::block_hash hash;
		lumex::election_status status;
	};

	// clang-format off
	class tag_hash {};
	class tag_root {};
	class tag_sequenced {};

	using ordered_entries = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<entry, lumex::qualified_root, &entry::root>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<entry, lumex::block_hash, &entry::hash>>>>;
	// clang-format on
	ordered_entries entries;

	std::size_t const max_size;

	mutable lumex::mutex mutex;
};
}
