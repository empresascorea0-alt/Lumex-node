#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/lib/observer_set.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/node/scheduler/bucket.hpp>
#include <lumex/node/scheduler/priority_pool.hpp>

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <thread>

namespace lumex::scheduler
{
class priority_config
{
public:
	lumex::error deserialize (lumex::tomlconfig &);
	lumex::error serialize (lumex::tomlconfig &) const;

public:
	bool enable{ true };

	// Pool configuration
	size_t max_blocks{ 1024 * 64 }; // Total shared pool size across all buckets
	size_t reserved_blocks{ 1024 * 8 }; // Reserved blocks per bucket

	// Election configuration
	size_t reserved_elections{ 100 }; // Guaranteed election slots per bucket
	size_t max_elections{ 150 }; // Maximum election slots per bucket when AEC has space

	std::chrono::milliseconds cleanup_interval{ 100 };
};

class priority final
{
public:
	priority (lumex::node_config &, lumex::node &, lumex::ledger &, lumex::ledger_notifications &, lumex::bucketing &, lumex::active_elections &, lumex::cementing_set &, lumex::stats &, lumex::logger &);
	~priority ();

	void start ();
	void stop ();

	/**
	 * Activates the first unconfirmed block of \p account
	 * @return true if account was activated
	 */
	bool activate (lumex::secure::transaction const &, lumex::account const &);
	bool activate (lumex::secure::transaction const &, lumex::account const &, lumex::account_info const &, lumex::confirmation_height_info const &);
	bool activate_successors (lumex::secure::transaction const &, lumex::block const &);

	bool contains (lumex::block_hash const &) const;
	void notify ();
	size_t size () const;
	bool empty () const;

	size_t pool_size () const;
	size_t pool_size (lumex::bucket_index) const;
	size_t election_count (lumex::bucket_index) const;

	lumex::container_info container_info () const;

public: // Testing
	bool push (std::shared_ptr<lumex::block> const & block, lumex::bucket_index, lumex::priority_timestamp);

public: // Events
	// Triggered when blocks are activated (elections started) in the scheduler run loop
	lumex::observer_set<std::deque<lumex::block_hash>> batch_activated;

private: // Dependencies
	priority_config const & config;
	lumex::node & node;
	lumex::ledger & ledger;
	lumex::ledger_notifications & ledger_notifications;
	lumex::bucketing & bucketing;
	lumex::active_elections & active;
	lumex::cementing_set & cementing_set;
	lumex::stats & stats;
	lumex::logger & logger;

private:
	void run ();
	void run_cleanup ();
	bool predicate () const;

private:
	std::map<lumex::bucket_index, std::unique_ptr<scheduler::bucket>> buckets;
	priority_pool pool;

	bool stopped{ false };
	lumex::condition_variable condition;
	mutable lumex::mutex mutex;
	std::thread thread;
	std::thread cleanup_thread;
};
}
