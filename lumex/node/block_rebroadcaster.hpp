#pragma once

#include <lumex/lib/interval.hpp>
#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/node/fwd.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <thread>
#include <unordered_set>

using namespace std::chrono_literals;

namespace mi = boost::multi_index;

/**
 * Rebroadcasts blocks that are in active elections to help propagate them across the network.
 * Blocks are queued for rebroadcast when elections start, become stale and at regular intervals as configured by block_broadcast_interval network parameter.
 * A cooldown mechanism prevents the same block from being rebroadcast too frequently.
 */
namespace lumex
{
class block_rebroadcaster_config final
{
public:
	lumex::error deserialize (lumex::tomlconfig &);
	lumex::error serialize (lumex::tomlconfig &) const;

public:
	bool enable{ true };
	size_t max_queue{ 1024 * 4 }; // Maximum number of blocks to keep in queue for processing
	size_t max_history{ 1024 * 128 }; // Maximum number of recently broadcast hashes to keep
	std::chrono::milliseconds rebroadcast_cooldown{ 60s }; // Minimum time between rebroadcasts for the same block
};

class block_rebroadcaster_index
{
public:
	explicit block_rebroadcaster_index (block_rebroadcaster_config const &);

	// Returns true if block should be rebroadcast, records the rebroadcast
	bool check_and_record (lumex::block_hash const & hash, std::chrono::steady_clock::time_point now);
	size_t cleanup (std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now ());

	bool contains (lumex::block_hash const & hash) const;
	size_t size () const;

private:
	block_rebroadcaster_config const & config;

	struct entry
	{
		lumex::block_hash hash;
		std::chrono::steady_clock::time_point timestamp;
	};

	// clang-format off
	class tag_sequenced {};
	class tag_hash {};

	using ordered_history = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<entry, lumex::block_hash, &entry::hash>>
	>>;
	// clang-format on

	ordered_history history;
};

class block_rebroadcaster final
{
public:
	block_rebroadcaster (block_rebroadcaster_config const &, lumex::node_flags const &, lumex::active_elections &, lumex::network &, lumex::stats &, lumex::logger &);
	~block_rebroadcaster ();

	void start ();
	void stop ();

	bool push (std::shared_ptr<lumex::block> const &);

	lumex::container_info container_info () const;

public: // Dependencies
	block_rebroadcaster_config const & config;
	lumex::node_flags const & flags;
	lumex::active_elections & active;
	lumex::network & network;
	lumex::stats & stats;
	lumex::logger & logger;

private:
	void run ();
	std::shared_ptr<lumex::block> next ();
	void cleanup ();
	size_t broadcast (std::shared_ptr<lumex::block> const &);
	bool check_capacity () const;

private:
	// Simple FIFO queue of blocks to rebroadcast
	std::deque<std::shared_ptr<lumex::block>> queue;
	std::unordered_set<lumex::block_hash> queue_dedup; // Avoid duplicates in the queue

	lumex::locked<block_rebroadcaster_index> rebroadcasts;

private:
	bool stopped{ false };
	std::condition_variable condition;
	mutable std::mutex mutex;
	std::thread thread;

	lumex::interval cleanup_interval;
	lumex::interval log_interval;
	lumex::interval capacity_warning_interval;
};
}
