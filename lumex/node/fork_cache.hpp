#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/node/fwd.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <deque>
#include <memory>
#include <mutex>

namespace mi = boost::multi_index;

namespace lumex
{
class fork_cache_config final
{
public:
	lumex::error deserialize (lumex::tomlconfig &);
	lumex::error serialize (lumex::tomlconfig &) const;

public:
	size_t max_size{ 1024 * 16 };
	size_t max_forks_per_root{ 10 };
};

class fork_cache final
{
public:
	fork_cache (fork_cache_config const &, lumex::stats &);

	void put (std::shared_ptr<lumex::block> fork);
	std::deque<std::shared_ptr<lumex::block>> get (lumex::qualified_root const & root) const;

	size_t size () const;
	bool contains (lumex::qualified_root const & root) const;

	lumex::container_info container_info () const;

private:
	fork_cache_config const & config;
	lumex::stats & stats;

	struct entry
	{
		lumex::qualified_root root;
		mutable std::deque<std::shared_ptr<lumex::block>> forks;
	};

	// clang-format off
	class tag_sequenced {};
	class tag_root {};

	using ordered_forks = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<entry, lumex::qualified_root, &entry::root>>
	>>;
	// clang-format on

	ordered_forks roots;

	mutable std::mutex mutex;
};
}