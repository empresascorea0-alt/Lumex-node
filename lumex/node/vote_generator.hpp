#pragma once

#include <lumex/lib/interval.hpp>
#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/lib/thread_roles.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/node/fair_queue.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/node/transport/traffic_type.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/network_params.hpp>
#include <lumex/secure/voting_policy.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <condition_variable>
#include <deque>
#include <thread>
#include <unordered_map>

namespace mi = boost::multi_index;

namespace lumex
{
class vote_generator_config final
{
public:
	lumex::error serialize (lumex::tomlconfig &) const;
	lumex::error deserialize (lumex::tomlconfig &);

public:
	size_t max_queue{ 1024 * 32 };
	size_t batch_size{ 256 };
	size_t normal_threads{ 2 };
	std::chrono::milliseconds delay{ 100ms };
};

/**
 * Fair queue over balance buckets with deduplication by root.
 * Replaces existing entry when a new hash arrives for the same root.
 * Holds candidates for vote generation.
 */
class vote_generator_index final
{
public:
	using entry = std::pair<lumex::qualified_root, lumex::block_hash>;

	explicit vote_generator_index (size_t max_size_per_bucket);

	/// Push a request. Returns true if added or replaced, false if duplicate (same root+hash) or queue full
	bool push (lumex::qualified_root const & root, lumex::block_hash const & hash, lumex::bucket_index bucket);

	/// Remove and return up to `count` valid entries (stale entries from replacements are skipped)
	std::deque<entry> next_batch (size_t count);

	size_t size () const;
	bool empty () const;

private:
	lumex::fair_queue<entry, lumex::bucket_index> queue;
	std::unordered_map<lumex::qualified_root, lumex::block_hash> dedup;
};

/**
 * Holds vote permits in FIFO order with deduplication by root
 * Provides batch extraction from the front
 */
class vote_broadcast_index final
{
public:
	explicit vote_broadcast_index (size_t max_size);

	/// Insert a permit keyed by root. If same root+hash exists, the new one is dropped. If same root but different hash (conflict), the old entry is replaced.
	bool push (lumex::qualified_root const & root, lumex::vote_permit permit);

	/// Remove the entry for the given root. Returns true if erased
	bool erase (lumex::qualified_root const & root);

	/// Remove and return up to `count` entries from the front (FIFO order)
	std::vector<lumex::vote_permit> next_batch (size_t count);

	size_t size () const;
	bool empty () const;

private:
	size_t const max_size;

	struct entry
	{
		lumex::qualified_root root;
		lumex::vote_permit permit;
	};

	// clang-format off
	class tag_sequenced {};
	class tag_root {};

	using ordered_entries = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<entry, lumex::qualified_root, &entry::root>>
	>>;
	// clang-format on

	ordered_entries entries;
};

/**
 * Threaded queue that wraps vote_generator_index.
 * Worker threads pull batches and invoke the process_batch callback.
 */
class vote_generator_verifier final
{
public:
	using entry = vote_generator_index::entry;

public:
	vote_generator_verifier (size_t max_size_per_bucket, size_t batch_size, size_t thread_count, lumex::thread_role::name thread_role);
	~vote_generator_verifier ();

	void start ();
	void stop ();

	bool push (lumex::qualified_root const &, lumex::block_hash const &, lumex::bucket_index);

	size_t size () const;
	bool empty () const;

public: // Callbacks
	/// Called on worker threads with a batch of entries.
	std::function<void (std::deque<entry>)> process_batch;

private:
	void run ();

	size_t const batch_size;
	size_t const thread_count;
	lumex::thread_role::name const thread_role;

	vote_generator_index index;

	mutable lumex::mutex mutex;
	lumex::condition_variable condition;
	std::vector<std::thread> threads;
	bool stopped{ false };
};

/**
 * Threaded queue that wraps vote_broadcast_index.
 * A single thread pulls batches when a size threshold is reached or a timer expires,
 * then invokes the broadcast_batch callback.
 */
class vote_generator_broadcaster final
{
public:
	using entry = lumex::vote_permit;

public:
	vote_generator_broadcaster (size_t max_size, size_t batch_threshold, std::chrono::milliseconds delay, lumex::thread_role::name thread_role);
	~vote_generator_broadcaster ();

	void start ();
	void stop ();

	bool push (lumex::qualified_root const &, lumex::vote_permit const &);

	size_t size () const;
	bool empty () const;

public: // Callbacks
	/// Called on broadcast thread with a batch of permits.
	std::function<void (std::vector<lumex::vote_permit>)> broadcast_batch;

	/// Called before extracting a batch. Returns false to defer broadcasting.
	std::function<bool ()> check_capacity;

private:
	void run ();
	bool predicate () const;

	size_t const batch_threshold;
	std::chrono::milliseconds const delay;
	lumex::thread_role::name const thread_role;

	vote_broadcast_index index;

	std::chrono::steady_clock::time_point last_broadcast{ std::chrono::steady_clock::now () };

	mutable lumex::mutex mutex;
	lumex::condition_variable condition;
	std::thread thread;
	bool stopped{ false };
};

/**
 * Unified vote generator for both normal and final votes
 */
class vote_generator final
{
public:
	vote_generator (vote_generator_config const &, lumex::voting_policy &, lumex::ledger &, lumex::wallet::wallets &, lumex::vote_processor &, lumex::network &, lumex::stats &, lumex::logger &, std::shared_ptr<lumex::transport::channel>);
	~vote_generator ();

	void start ();
	void stop ();

	void vote (lumex::qualified_root const &, lumex::block_hash const &, lumex::bucket_index, lumex::vote_type);
	void vote_normal (lumex::qualified_root const &, lumex::block_hash const &, lumex::bucket_index);
	void vote_final (lumex::qualified_root const &, lumex::block_hash const &, lumex::bucket_index);

	lumex::container_info container_info () const;

private:
	void process_normal (std::deque<vote_generator_verifier::entry> batch);
	void process_final (std::deque<vote_generator_verifier::entry> batch);
	void broadcast_normal (std::vector<lumex::vote_permit> batch);
	void broadcast_final (std::vector<lumex::vote_permit> batch);
	void broadcast_vote (std::shared_ptr<lumex::vote> const & vote) const;
	bool check_normal_capacity ();
	bool check_final_capacity ();

private: // Dependencies
	vote_generator_config const & config;
	lumex::voting_policy & policy;
	lumex::ledger & ledger;
	lumex::wallet::wallets & wallets;
	lumex::vote_processor & vote_processor;
	lumex::network & network;
	lumex::stats & stats;
	lumex::logger & logger;

	std::shared_ptr<lumex::transport::channel> inproc_channel;

private:
	vote_generator_verifier normal_verifier;
	vote_generator_broadcaster normal_broadcaster;

	vote_generator_verifier final_verifier;
	vote_generator_broadcaster final_broadcaster;

	lumex::interval normal_capacity_log_interval;
	lumex::interval final_capacity_log_interval;
};
}
