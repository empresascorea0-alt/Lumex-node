#pragma once

#include <lumex/lib/config.hpp>
#include <lumex/lib/interval.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/observer_set.hpp>
#include <lumex/lib/thread_pool.hpp>
#include <lumex/node/active_elections_index.hpp>
#include <lumex/node/election_behavior.hpp>
#include <lumex/node/election_status.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/node/recently_cemented_cache.hpp>
#include <lumex/node/recently_confirmed_cache.hpp>
#include <lumex/node/vote_router.hpp>
#include <lumex/node/vote_with_weight_info.hpp>
#include <lumex/secure/common.hpp>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <thread>
#include <unordered_map>

using namespace std::chrono_literals;
namespace lumex
{
class active_elections_config final
{
public:
	explicit active_elections_config (lumex::network_constants const &);

	lumex::error deserialize (lumex::tomlconfig & toml);
	lumex::error serialize (lumex::tomlconfig & toml) const;

public:
	// Maximum number of simultaneous active elections (AEC size)
	std::size_t size{ 5000 };
	// Limit of hinted elections as percentage of `active_elections_size`
	std::size_t hinted_limit_percentage{ 20 };
	// Limit of optimistic elections as percentage of `active_elections_size`
	std::size_t optimistic_limit_percentage{ 10 };
	// Maximum cache size for recently_confirmed
	std::size_t confirmation_cache{ 1024 * 64 };
	// Maximum size of election winner details set
	std::size_t max_election_winners{ 1024 * 16 };

	std::chrono::milliseconds checkup_interval{ 1s };
	std::chrono::seconds stale_threshold{ lumex::is_dev_run () ? 1s : 60s };
};

/**
 * Core class for determining consensus
 * Holds all active blocks i.e. recently added blocks that need confirmation
 */
class active_elections final
{
public:
	using erased_callback_t = std::function<void (std::shared_ptr<lumex::election>)>;

public:
	active_elections (lumex::node &, lumex::ledger_notifications &, lumex::cementing_set &);
	~active_elections ();

	void start ();
	void stop ();

	struct insert_result
	{
		std::shared_ptr<lumex::election> election;
		bool inserted;
	};

	/// Starts new election
	insert_result insert (
	std::shared_ptr<lumex::block> const &,
	lumex::election_behavior = lumex::election_behavior::priority,
	lumex::bucket_index bucket = 0,
	lumex::priority_timestamp priority = 0,
	erased_callback_t = nullptr);

	// Notify this container about a new block (potential fork)
	bool publish (std::shared_ptr<lumex::block> const &);

	// Trigger an immediate election update (e.g. after it is confirmed)
	bool trigger (lumex::qualified_root const &);

	/// Is the root of this block in the roots container
	bool active (lumex::block const &) const;
	bool active (lumex::qualified_root const &) const;

	std::shared_ptr<lumex::election> election (lumex::qualified_root const &) const;

	/// Returns a list of elections sorted by difficulty
	std::vector<std::shared_ptr<lumex::election>> list_active (std::size_t max_count = std::numeric_limits<std::size_t>::max ());

	bool erase (lumex::block const &);
	bool erase (lumex::qualified_root const &);

	bool empty () const;

	size_t size () const;
	size_t size (lumex::election_behavior) const;
	size_t size (lumex::election_behavior, lumex::bucket_index) const;
	size_t stale_count () const;

	/// Maximum number of elections that should be present in this container
	/// NOTE: This is only a soft limit, it is possible for this container to exceed this count
	int64_t limit (lumex::election_behavior behavior) const;

	/// How many election slots are available for specified election type
	int64_t vacancy (lumex::election_behavior behavior) const;

	lumex::container_info container_info () const;

public: // Events
	lumex::observer_set<> vacancy_updated;
	lumex::observer_set<std::shared_ptr<lumex::election>, lumex::bucket_index, lumex::priority_timestamp> election_started;
	lumex::observer_set<std::shared_ptr<lumex::election>> election_erased;
	lumex::observer_set<std::shared_ptr<lumex::election>> election_stale;

private:
	bool predicate () const;
	void run ();
	void run_checkup ();
	void tick_elections (lumex::unique_lock<lumex::mutex> &);
	void checkup_elections (lumex::unique_lock<lumex::mutex> &);

	// Erase all blocks from active and, if not confirmed, clear digests from network filters
	void erase_election (lumex::unique_lock<lumex::mutex> & lock_a, std::shared_ptr<lumex::election>);

	struct block_cemented_result
	{
		std::shared_ptr<lumex::election> election;
		lumex::election_status status;
		std::vector<lumex::vote_with_weight_info> votes;
	};

	block_cemented_result block_cemented (std::shared_ptr<lumex::block> const & block, lumex::block_hash const & confirmation_root, std::shared_ptr<lumex::election> const & source_election);
	void notify_observers (lumex::secure::transaction const &, lumex::election_status const & status, std::vector<lumex::vote_with_weight_info> const & votes) const;

	std::shared_ptr<lumex::election> election_impl (lumex::qualified_root const &) const;
	std::vector<std::shared_ptr<lumex::election>> list_active_impl (std::size_t max_count = std::numeric_limits<std::size_t>::max ()) const;

private: // Dependencies
	active_elections_config const & config;
	lumex::node & node;
	lumex::ledger_notifications & ledger_notifications;
	lumex::cementing_set & cementing_set;

public:
	lumex::active_elections_index index;

	std::unordered_map<lumex::qualified_root, erased_callback_t> erased_callbacks;

	lumex::recently_confirmed_cache recently_confirmed;
	lumex::recently_cemented_cache recently_cemented;

private:
	mutable lumex::mutex mutex{ mutex_identifier (mutexes::active) };
	lumex::condition_variable condition;
	bool stopped{ false };
	std::thread thread;
	std::thread checkup_thread;

	lumex::thread_pool workers;

	lumex::interval stale_interval;
	lumex::interval warning_interval;

public: // Tests
	void clear ();
};

lumex::stat::type to_stat_type (lumex::election_state);
lumex::stat::detail to_stat_detail (lumex::election_state);
}
