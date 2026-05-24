#pragma once

#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/node/fwd.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <memory>
#include <vector>

namespace mi = boost::multi_index;

namespace lumex
{
class voting_constants;
}

namespace lumex
{
class local_vote_history final
{
	class local_vote final
	{
	public:
		local_vote (lumex::root const & root_a, lumex::block_hash const & hash_a, std::shared_ptr<lumex::vote> const & vote_a) :
			root (root_a),
			hash (hash_a),
			vote (vote_a)
		{
		}
		lumex::root root;
		lumex::block_hash hash;
		std::shared_ptr<lumex::vote> vote;
	};

public:
	local_vote_history (lumex::voting_constants const & constants) :
		constants{ constants }
	{
	}
	void add (lumex::root const & root_a, lumex::block_hash const & hash_a, std::shared_ptr<lumex::vote> const & vote_a);
	void erase (lumex::root const & root_a);

	std::vector<std::shared_ptr<lumex::vote>> votes (lumex::root const & root_a, lumex::block_hash const & hash_a, bool const is_final_a = false) const;
	bool exists (lumex::root const &) const;
	std::size_t size () const;

	lumex::container_info container_info () const;

private:
	// clang-format off
	boost::multi_index_container<local_vote,
	mi::indexed_by<
		mi::hashed_non_unique<mi::tag<class tag_root>,
			mi::member<local_vote, lumex::root, &local_vote::root>>,
		mi::sequenced<mi::tag<class tag_sequence>>>>
	history;
	// clang-format on

	lumex::voting_constants const & constants;
	void clean ();
	std::vector<std::shared_ptr<lumex::vote>> votes (lumex::root const & root_a) const;
	// Only used in Debug
	bool consistency_check (lumex::root const &) const;
	mutable lumex::mutex mutex;

	friend class local_vote_history_basic_Test;
};
}
