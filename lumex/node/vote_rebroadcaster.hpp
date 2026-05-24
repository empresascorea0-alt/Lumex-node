#pragma once

#include <lumex/lib/interval.hpp>
#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/lib/vote.hpp>
#include <lumex/node/fair_queue.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/node/rep_tiers.hpp>
#include <lumex/node/wallet.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <thread>
#include <unordered_map>

namespace mi = boost::multi_index;

namespace lumex
{
class vote_rebroadcaster_config final
{
public:
	lumex::error deserialize (lumex::tomlconfig &);
	lumex::error serialize (lumex::tomlconfig &) const;

public:
	bool enable{ true };
	size_t max_queue{ 1024 * 4 }; // Maximum number of votes to keep in queue for processing
	size_t max_history{ 1024 * 32 }; // Maximum number of recently broadcast hashes to keep per representative
	size_t max_representatives{ 100 }; // Maximum number of representatives to track rebroadcasts for
	std::chrono::milliseconds rebroadcast_threshold{ 1000 * 90 }; // Minimum amount of time between rebroadcasts for the same hash from the same representative (milliseconds)
	size_t priority_coefficient{ 2 }; // Priority coefficient for prioritizing votes from representative tiers
};

class vote_rebroadcaster_index
{
public:
	explicit vote_rebroadcaster_index (vote_rebroadcaster_config const &);

	enum class result
	{
		ok,
		already_rebroadcasted,
		representatives_full,
		rebroadcast_unnecessary,
	};

	result check_and_record (std::shared_ptr<lumex::vote> const & vote, lumex::uint128_t rep_weight, std::chrono::steady_clock::time_point now);

	using rep_query = std::function<std::pair<bool, lumex::uint128_t> (lumex::account const &)>; // Returns <should keep, rep weight>
	size_t cleanup (rep_query);

	bool contains_vote (lumex::block_hash const & vote_hash) const;
	bool contains_representative (lumex::account const & representative) const;
	bool contains_block (lumex::account const & representative, lumex::block_hash const & block_hash) const;

	size_t representatives_count () const;
	size_t total_history () const;
	size_t total_hashes () const;

private:
	vote_rebroadcaster_config const & config;

	struct rebroadcast_entry
	{
		lumex::block_hash block_hash;
		lumex::vote_timestamp vote_timestamp;
		std::chrono::steady_clock::time_point timestamp;
	};

	// clang-format off
	class tag_sequenced {};
	class tag_vote_hash {};
	class tag_block_hash {};

	// Tracks rebroadcast history for individual block hashes
	using ordered_rebroadcasts = boost::multi_index_container<rebroadcast_entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_block_hash>,
			mi::member<rebroadcast_entry, lumex::block_hash, &rebroadcast_entry::block_hash>>
	>>;

	// Tracks rebroadcast history for full votes
	using ordered_hashes = boost::multi_index_container<lumex::block_hash,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_vote_hash>,
			mi::identity<lumex::block_hash>>
	>>;
	// clang-format on

	struct representative_entry
	{
		lumex::account representative;
		lumex::uint128_t weight;

		mutable ordered_rebroadcasts history;
		mutable ordered_hashes hashes;
	};

	// clang-format off
	class tag_account {};
	class tag_weight {};

	using ordered_representatives = boost::multi_index_container<representative_entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_account>,
			mi::member<representative_entry, lumex::account, &representative_entry::representative>>,
		mi::ordered_non_unique<mi::tag<tag_weight>,
			mi::member<representative_entry, lumex::uint128_t, &representative_entry::weight>>
	>>;
	// clang-format on

	ordered_representatives index;
};

lumex::stat::detail to_stat_detail (lumex::vote_rebroadcaster_index::result);

class vote_rebroadcaster final
{
public:
	vote_rebroadcaster (vote_rebroadcaster_config const &, lumex::node_flags const &, lumex::ledger &, lumex::vote_router &, lumex::network &, lumex::wallet::wallets &, lumex::rep_tiers &, lumex::stats &, lumex::logger &);
	~vote_rebroadcaster ();

	void start ();
	void stop ();

	bool push (std::shared_ptr<lumex::vote> const &, lumex::rep_tier);

	lumex::container_info container_info () const;

public: // Dependencies
	vote_rebroadcaster_config const & config;
	lumex::node_flags const & flags;
	lumex::ledger & ledger;
	lumex::vote_router & vote_router;
	lumex::network & network;
	lumex::wallet::wallets & wallets;
	lumex::rep_tiers & rep_tiers;
	lumex::stats & stats;
	lumex::logger & logger;

private:
	void run ();
	void cleanup ();
	bool process (std::shared_ptr<lumex::vote> const &);
	std::pair<std::shared_ptr<lumex::vote>, lumex::rep_tier> next ();
	size_t broadcast (std::shared_ptr<lumex::vote> const &);
	bool check_capacity () const;

private:
	// Queue of recently processed votes to potentially rebroadcast
	lumex::fair_queue<std::shared_ptr<lumex::vote>, lumex::rep_tier> queue;
	std::unordered_set<lumex::signature> queue_hashes; // Avoids queuing the same vote multiple times

	lumex::locked<vote_rebroadcaster_index> rebroadcasts;

private:
	std::atomic<bool> has_principal{ false };
	lumex::wallet::wallet_representatives reps;
	lumex::interval refresh_interval;
	lumex::interval cleanup_interval;

	bool stopped{ false };
	std::condition_variable condition;
	mutable std::mutex mutex;
	std::thread thread;

	lumex::interval log_interval;
	lumex::interval capacity_warning_interval;
};
}