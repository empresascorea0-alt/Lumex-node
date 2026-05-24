#pragma once

#include <lumex/lib/interval.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/lib/observer_set.hpp>
#include <lumex/lib/thread_pool.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/fwd.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_set>

using namespace std::chrono_literals;
namespace mi = boost::multi_index;

namespace lumex
{
class cementing_set_config final
{
public:
	lumex::error deserialize (lumex::tomlconfig &);
	lumex::error serialize (lumex::tomlconfig &) const;

public:
	bool enable{ true };
	size_t batch_size{ 256 };

	/** Maximum number of dependent blocks to be stored in memory during processing */
	size_t max_blocks{ 128 * 1024 };
	size_t max_queued_notifications{ 8 };

	/** Maximum number of failed blocks to wait for requeuing */
	size_t max_deferred{ 16 * 1024 };
	/** Max age of deferred blocks before they are dropped */
	std::chrono::seconds deferred_age_cutoff{ 15min };
};

/**
 * Set of blocks to be durably confirmed
 */
class cementing_set final
{
	friend class confirmation_heightDeathTest_missing_block_Test;
	friend class confirmation_height_pruned_source_Test;

public:
	cementing_set (cementing_set_config const &, lumex::ledger &, lumex::ledger_notifications &, lumex::stats &, lumex::logger &);
	~cementing_set ();

	void start ();
	void stop ();

	// Adds a block to the set of blocks to be confirmed
	bool add (lumex::block_hash const & hash, std::shared_ptr<lumex::election> const & election = nullptr);

	// Added blocks will remain in this set until after ledger has them marked as confirmed.
	bool contains (lumex::block_hash const & hash) const;
	std::size_t size () const;
	std::size_t deferred_size () const;

	lumex::container_info container_info () const;

public: // Events
	struct context
	{
		std::shared_ptr<lumex::block> block;
		lumex::block_hash confirmation_root;
		std::shared_ptr<lumex::election> election;
	};

	lumex::observer_set<std::deque<context> const &> batch_cemented;
	lumex::observer_set<std::deque<lumex::block_hash> const &> already_cemented;
	lumex::observer_set<lumex::block_hash> cementing_failed;

	lumex::observer_set<std::shared_ptr<lumex::block>> cemented_observers;

private: // Dependencies
	cementing_set_config const & config;
	lumex::ledger & ledger;
	lumex::ledger_notifications & ledger_notifications;
	lumex::stats & stats;
	lumex::logger & logger;

private:
	struct entry
	{
		lumex::block_hash hash;
		std::shared_ptr<lumex::election> election;
		std::chrono::steady_clock::time_point timestamp{ std::chrono::steady_clock::now () };
	};

	void run ();
	void run_batch (std::unique_lock<std::mutex> &);
	std::deque<entry> next_batch (size_t max_count);
	void cleanup (std::unique_lock<std::mutex> &);

private:
	// clang-format off
	class tag_hash {};
	class tag_sequenced {};

	using ordered_entries = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<entry, lumex::block_hash, &entry::hash>>
	>>;
	// clang-format on

	// Blocks that are ready to be cemented
	ordered_entries set;
	// Blocks that could not be cemented immediately (e.g. waiting for rollbacks to complete)
	ordered_entries deferred;

	// Blocks that are being cemented in the current batch
	std::unordered_set<lumex::block_hash> current;

	std::atomic<bool> stopped{ false };
	mutable std::mutex mutex;
	std::condition_variable condition;
	std::thread thread;

	lumex::thread_pool workers;

	lumex::interval log_interval;
};
}
