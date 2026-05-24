#pragma once

#include <lumex/lib/interval.hpp>
#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/fwd.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

namespace mi = boost::multi_index;

namespace lumex
{
class vote_cache_config final
{
public:
	lumex::error deserialize (lumex::tomlconfig & toml);
	lumex::error serialize (lumex::tomlconfig & toml) const;

public:
	std::size_t max_size{ 1024 * 64 };
	std::size_t max_voters{ 64 };
	std::chrono::seconds age_cutoff{ 15 * 60 };
};

/**
 * Stores votes associated with a single block hash
 */
class vote_cache_entry final
{
private:
	struct voter_entry
	{
		lumex::account representative;
		lumex::uint128_t weight;
		std::shared_ptr<lumex::vote> vote;
	};

public:
	explicit vote_cache_entry (lumex::block_hash const & hash);

	/**
	 * Adds a vote into a list, checks for duplicates and updates timestamp if new one is greater
	 * @return true if current tally changed, false otherwise
	 */
	bool vote (std::shared_ptr<lumex::vote> const & vote, lumex::uint128_t const & rep_weight, std::size_t max_voters);

	std::size_t size () const;
	std::vector<std::shared_ptr<lumex::vote>> votes () const;

public: // Keep accessors inlined
	lumex::block_hash hash () const
	{
		return hash_m;
	}
	std::chrono::steady_clock::time_point last_vote () const
	{
		return last_vote_m;
	}
	lumex::uint128_t tally () const
	{
		return tally_m;
	}
	lumex::uint128_t final_tally () const
	{
		return final_tally_m;
	}

private:
	bool vote_impl (std::shared_ptr<lumex::vote> const & vote, lumex::uint128_t const & rep_weight, std::size_t max_voters);
	std::pair<lumex::uint128_t, lumex::uint128_t> calculate_tally () const; // <tally, final_tally>

	// clang-format off
	class tag_representative {};
	class tag_weight {};
	// clang-format on

	// clang-format off
	using ordered_voters = boost::multi_index_container<voter_entry,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_representative>,
			mi::member<voter_entry, lumex::account, &voter_entry::representative>>,
		mi::ordered_non_unique<mi::tag<tag_weight>,
			mi::member<voter_entry, lumex::uint128_t, &voter_entry::weight>>
	>>;
	// clang-format on
	ordered_voters voters;

	lumex::block_hash const hash_m;
	std::chrono::steady_clock::time_point last_vote_m{};
	lumex::uint128_t tally_m{ 0 };
	lumex::uint128_t final_tally_m{ 0 };
};

class vote_cache final
{
public:
	using entry = vote_cache_entry;

public:
	explicit vote_cache (vote_cache_config const &, lumex::stats &);

	/**
	 * Adds a new vote to cache
	 */
	void insert (
	std::shared_ptr<lumex::vote> const & vote,
	std::unordered_map<lumex::block_hash, lumex::vote_code> const & results = {});

	/**
	 * Tries to find an entry associated with block hash
	 */
	std::vector<std::shared_ptr<lumex::vote>> find (lumex::block_hash const & hash) const;
	bool contains (lumex::block_hash const & hash) const;

	/**
	 * Removes an entry associated with block hash, does nothing if entry does not exist
	 * @return true if hash existed and was erased, false otherwise
	 */
	bool erase (lumex::block_hash const & hash);
	void clear ();

	std::size_t size () const;
	bool empty () const;

	struct top_entry
	{
		lumex::block_hash hash;
		lumex::uint128_t tally;
		lumex::uint128_t final_tally;
	};

	/**
	 * Returns blocks with highest observed tally
	 * The blocks are sorted in descending order by final tally, then by tally
	 * @param min_tally minimum tally threshold, entries below with their voting weight below this will be ignored
	 */
	std::deque<top_entry> top (lumex::uint128_t const & min_tally);

	lumex::container_info container_info () const;

public:
	/**
	 * Function used to query rep weight for tally calculation
	 */
	std::function<lumex::uint128_t (lumex::account const &)> rep_weight_query{ [] (lumex::account const & rep) { debug_assert (false); return 0; } };

private: // Dependencies
	vote_cache_config const & config;
	lumex::stats & stats;

private:
	void insert_impl (std::shared_ptr<lumex::vote> const &, lumex::block_hash const & hash, lumex::uint128_t const & rep_weight);
	void cleanup ();

	// clang-format off
	class tag_sequenced {};
	class tag_hash {};
	class tag_tally {};
	// clang-format on

	// clang-format off
	using ordered_cache = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::const_mem_fun<entry, lumex::block_hash, &entry::hash>>,
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::ordered_non_unique<mi::tag<tag_tally>,
			mi::const_mem_fun<entry, lumex::uint128_t, &entry::tally>, std::greater<>> // DESC
	>>;
	// clang-format on
	ordered_cache cache;

	mutable lumex::mutex mutex;
	lumex::interval cleanup_interval;
};
}
