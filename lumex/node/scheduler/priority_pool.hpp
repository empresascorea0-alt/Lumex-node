#pragma once

#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/secure/common.hpp>

#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <cstddef>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>

namespace mi = boost::multi_index;

namespace lumex::scheduler
{
struct priority_entry
{
	std::shared_ptr<lumex::block> block;
	lumex::bucket_index bucket;
	lumex::priority_timestamp priority;
};

/*
 * A global pool of priority blocks to be scheduled for election
 * Blocks are grouped by their bucket and ordered by their priority (lower priority value goes first)
 * The benefit of a shared pool is that unused bucket capacity could be utilized by active buckets
 * The pool has a maximum size, and new blocks are only added if there is space or if bucket size is below reserved capacity
 */
class priority_pool final
{
public:
	explicit priority_pool (size_t max_size, size_t bucket_reserved);

	/// Add a block to the pool with given bucket and priority
	bool push (std::shared_ptr<lumex::block> const & block, lumex::bucket_index bucket, lumex::priority_timestamp priority);

	/// Peek or pop the next priority block (lower priority value goes first) for a given bucket
	std::optional<priority_entry> top (lumex::bucket_index bucket) const;
	std::optional<priority_entry> pop (lumex::bucket_index bucket);

	/// Get the best priority block for all non-empty buckets
	std::map<lumex::bucket_index, priority_entry> top_all () const;

	bool erase (lumex::block_hash const & hash);
	size_t erase_all (std::deque<lumex::block_hash> const & hashes);

	/// Evict the worst priority block (highest priority value) from a given bucket
	lumex::block_hash evict (lumex::bucket_index bucket);

	bool contains (lumex::block_hash const & hash) const;
	bool any (lumex::bucket_index bucket) const;

	size_t size () const;
	size_t size (lumex::bucket_index bucket) const;
	bool empty () const;
	bool empty (lumex::bucket_index bucket) const;

	void clear ();

	lumex::container_info container_info () const;

private:
	size_t const max_size;
	size_t const bucket_reserved;

	struct entry
	{
		lumex::bucket_index bucket;
		lumex::priority_timestamp priority;
		lumex::block_hash hash;
		std::shared_ptr<lumex::block> block;
	};

	// clang-format off
	class tag_bucket_priority {};
	class tag_hash {};
	class tag_priority {};

	using ordered_entries = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::ordered_non_unique<mi::tag<tag_bucket_priority>,
			mi::composite_key<entry,
				mi::member<entry, lumex::bucket_index, &entry::bucket>,
				mi::member<entry, lumex::priority_timestamp, &entry::priority>,
				mi::member<entry, lumex::block_hash, &entry::hash>
			>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<entry, lumex::block_hash, &entry::hash>>,
		mi::ordered_non_unique<mi::tag<tag_priority>,
			mi::member<entry, lumex::priority_timestamp, &entry::priority>>
	>>;
	// clang-format on

	ordered_entries pool;

	// Keep track of bucket sizes for efficient lookups
	std::unordered_map<lumex::bucket_index, size_t> bucket_sizes;
};
}