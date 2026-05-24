#pragma once

#include <lumex/lib/id_dispenser.hpp>
#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/lib/stats_enums.hpp>
#include <lumex/node/election_status.hpp>
#include <lumex/node/vote_with_weight_info.hpp>
#include <lumex/secure/common.hpp>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace lumex
{
class block;
class channel;
class confirmation_solicitor;
enum class election_behavior;
class inactive_cache_information;
class node;
enum class vote_code;
enum class vote_source;

class vote_info final
{
public:
	std::chrono::steady_clock::time_point time;
	uint64_t timestamp;
	lumex::block_hash hash;
};

// map of vote weight per block, ordered greater first
using tally_t = std::map<lumex::uint128_t, std::shared_ptr<lumex::block>, std::greater<lumex::uint128_t>>;

struct election_extended_status final
{
	lumex::election_status status;
	std::unordered_map<lumex::account, lumex::vote_info> votes;
	std::unordered_map<lumex::block_hash, std::shared_ptr<lumex::block>> blocks;
	lumex::tally_t tally;

	void operator() (lumex::object_stream &) const;
};

enum class election_state
{
	passive, // only listening for incoming votes
	active, // actively request confirmations
	confirmed, // confirmed but still listening for votes
	expired_confirmed,
	expired_unconfirmed,
	cancelled,
};

std::string_view to_string (election_state);
lumex::stat::detail to_stat_detail (election_state);

class election final : public std::enable_shared_from_this<election>
{
	lumex::id_t const id{ lumex::next_id () }; // Track individual objects when tracing

private:
	// Minimum time between broadcasts of the current winner of an election, as a backup to requesting confirmations
	std::chrono::milliseconds base_latency () const;

	// Callbacks
	std::function<void (std::shared_ptr<lumex::block> const &)> confirmation_action;
	std::function<void (lumex::account const &)> vote_action;
	std::function<void (lumex::qualified_root const &)> update_action;

private: // State management
	static unsigned constexpr passive_duration_factor = 5;
	static unsigned constexpr active_request_count_min = 2;
	lumex::election_state state_m{ election_state::passive };

	std::chrono::steady_clock::time_point state_start{ std::chrono::steady_clock::now () };

	// These are modified while not holding the mutex from transition_time only
	std::chrono::steady_clock::time_point last_block{};
	lumex::block_hash last_block_hash{ 0 };
	std::chrono::steady_clock::time_point last_req{};
	/** The last time vote for this election was generated */
	std::chrono::steady_clock::time_point last_vote{};

	bool valid_change (lumex::election_state, lumex::election_state) const;
	bool state_change (lumex::election_state, lumex::election_state);

public: // State transitions
	// Returns true if the election should be cleaned up
	bool tick (lumex::confirmation_solicitor &);

	bool transition_active ();
	bool transition_priority ();
	bool cancel ();

public: // Status
	bool confirmed () const;
	bool failed () const;
	lumex::election_extended_status current_status () const;
	std::shared_ptr<lumex::block> winner () const;
	std::chrono::milliseconds duration () const;

	std::atomic<unsigned> confirmation_request_count{ 0 };
	std::atomic<unsigned> vote_broadcast_count{ 0 };

	lumex::tally_t tally () const;
	bool have_quorum (lumex::tally_t const &) const;

	// Guarded by mutex
	lumex::election_status status;

public: // Interface
	election (
	lumex::node &,
	std::shared_ptr<lumex::block> const & block,
	lumex::election_behavior behavior,
	lumex::bucket_index bucket = 0,
	std::function<void (std::shared_ptr<lumex::block> const &)> confirmation_action = nullptr,
	std::function<void (lumex::account const &)> vote_action = nullptr,
	std::function<void (lumex::qualified_root const &)> update_action = nullptr);

	std::shared_ptr<lumex::block> find (lumex::block_hash const &) const;
	/*
	 * Process vote. Internally uses cooldown to throttle non-final votes
	 * If the election reaches consensus, it will be confirmed
	 */
	lumex::vote_code vote (lumex::account const & representative, uint64_t timestamp, lumex::block_hash const & block_hash, lumex::vote_source);
	bool publish (std::shared_ptr<lumex::block> const & block_a);
	// Confirm this block if quorum is met
	void confirm_if_quorum (lumex::unique_lock<lumex::mutex> &);
	void try_confirm (lumex::block_hash const & hash);

	/**
	 * Broadcasts vote for the current winner of this election
	 * Checks if sufficient amount of time (`vote_generation_interval`) passed since the last vote generation
	 */
	void broadcast_vote ();
	lumex::vote_info get_last_vote (lumex::account const & account);
	void set_last_vote (lumex::account const & account, lumex::vote_info vote_info);
	lumex::election_status get_status () const;

	std::chrono::steady_clock::time_point get_election_start () const
	{
		return election_start;
	}
	std::chrono::steady_clock::time_point get_state_start () const
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		return state_start;
	}

private: // Dependencies
	lumex::node & node;

public: // Information
	uint64_t const height;
	lumex::root const root;
	lumex::qualified_root const qualified_root;
	lumex::account const account;
	lumex::bucket_index const bucket;

	std::vector<lumex::vote_with_weight_info> votes_with_weight () const;
	lumex::election_behavior behavior () const;
	lumex::election_state state () const;

	std::unordered_map<lumex::account, lumex::vote_info> votes () const;
	std::unordered_map<lumex::block_hash, std::shared_ptr<lumex::block>> blocks () const;
	std::unordered_set<lumex::block_hash> blocks_hashes () const;
	bool contains (lumex::block_hash const &) const;

	size_t voter_count () const;
	size_t block_count () const;

private:
	lumex::tally_t tally_impl () const;
	bool confirmed_locked () const;
	lumex::election_extended_status current_status_locked () const;
	// lock_a does not own the mutex on return
	void confirm_once (lumex::unique_lock<lumex::mutex> & lock_a);
	bool broadcast_block_predicate () const;
	void broadcast_block (lumex::confirmation_solicitor &);
	void send_confirm_req (lumex::confirmation_solicitor &);
	/**
	 * Broadcast vote for current election winner. Generates final vote if reached quorum or already confirmed
	 * Requires mutex lock
	 */
	void broadcast_vote_locked (lumex::unique_lock<lumex::mutex> & lock);
	void remove_votes (lumex::block_hash const &);
	void remove_block (lumex::block_hash const &);
	bool replace_by_weight (lumex::unique_lock<lumex::mutex> & lock_a, lumex::block_hash const &);
	std::chrono::milliseconds time_to_live () const;
	/**
	 * Calculates minimum time delay between subsequent votes when processing non-final votes
	 */
	std::chrono::seconds cooldown_time (lumex::uint128_t weight) const;
	/**
	 * Calculates time delay between broadcasting confirmation requests
	 */
	std::chrono::milliseconds confirm_req_time () const;

private:
	std::unordered_map<lumex::block_hash, std::shared_ptr<lumex::block>> last_blocks;
	std::unordered_map<lumex::account, lumex::vote_info> last_votes;
	std::atomic<bool> is_quorum{ false };
	mutable lumex::uint128_t final_weight{ 0 };
	mutable std::unordered_map<lumex::block_hash, lumex::uint128_t> last_tally;

	lumex::election_behavior behavior_m;
	std::chrono::steady_clock::time_point const election_start{ std::chrono::steady_clock::now () };

	mutable lumex::mutex mutex;

public: // Logging
	void operator() (lumex::object_stream &) const;

private: // Constants
	static std::size_t constexpr max_blocks{ 10 };

	friend class active_elections;
	friend class confirmation_solicitor;

public: // Only used in tests
	void force_confirm ();

	friend class confirmation_solicitor_different_hash_Test;
	friend class confirmation_solicitor_bypass_max_requests_cap_Test;
	friend class votes_add_existing_Test;
	friend class votes_add_old_Test;
};
}
