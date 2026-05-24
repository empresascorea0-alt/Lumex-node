#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>

namespace mi = boost::multi_index;

namespace lumex
{
class vote_spacing final
{
	class entry
	{
	public:
		lumex::root root;
		std::chrono::steady_clock::time_point time;
		lumex::block_hash hash;
	};

	boost::multi_index_container<entry,
	mi::indexed_by<
	mi::hashed_non_unique<mi::tag<class tag_root>,
	mi::member<entry, lumex::root, &entry::root>>,
	mi::ordered_non_unique<mi::tag<class tag_time>,
	mi::member<entry, std::chrono::steady_clock::time_point, &entry::time>>>>
	recent;
	std::chrono::milliseconds const delay;
	void trim ();

public:
	vote_spacing (std::chrono::milliseconds const & delay) :
		delay{ delay }
	{
	}
	bool votable (lumex::root const & root_a, lumex::block_hash const & hash_a) const;
	void flag (lumex::root const & root_a, lumex::block_hash const & hash_a);
	std::size_t size () const;
};
}
