#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/lib/observer_set.hpp>
#include <lumex/node/fwd.hpp>

#include <condition_variable>
#include <memory>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

namespace lumex
{
enum class vote_code
{
	invalid, // Vote is not signed correctly
	replay, // Vote does not have the highest timestamp, it's a replay
	vote, // Vote has the highest timestamp
	indeterminate, // Unknown if replay or vote
	ignored, // Vote is valid, but got ingored (e.g. due to cooldown)
	late, // Vote is late, the election is already confirmed and present in the recently confirmed set
};

lumex::stat::detail to_stat_detail (vote_code);
std::string_view to_string (vote_code);

enum class vote_source
{
	live,
	rebroadcast,
	cache,
};

lumex::stat::detail to_stat_detail (vote_source);
std::string_view to_string (vote_source);

/**
 * Routes votes to their associated elections.
 * Holds weak_ptr to elections as this container does not own them.
 * Routing entries are removed periodically if the weak_ptr has expired.
 */
class vote_router final
{
public:
	vote_router (lumex::vote_cache &, lumex::recently_confirmed_cache &);
	~vote_router ();

	void start ();
	void stop ();

	/**
	 * Add a route for 'hash' to 'election'.
	 * Existing routes will be replaced.
	 * Election must hold the block for the hash being passed in.
	 */
	void connect (lumex::block_hash const & hash, std::weak_ptr<lumex::election> election);
	/**
	 * Remove all routes to this election.
	 * @return number of routes removed
	 */
	std::size_t disconnect (lumex::election const & election);
	/**
	 * Remove route for hash.
	 * @return true if route existed and was removed
	 */
	bool disconnect (lumex::block_hash const & hash);

	/**
	 * Route vote to associated elections.
	 * Distinguishes replay votes, cannot be determined if the block is not in any election.
	 * If 'filter' parameter is non-zero, only elections for the specified hash are notified.
	 * This eliminates duplicate processing when triggering votes from the vote_cache as the result of a specific election being created.
	 */
	std::unordered_map<lumex::block_hash, lumex::vote_code> vote (std::shared_ptr<lumex::vote> const &, lumex::vote_source = lumex::vote_source::live, lumex::block_hash filter = { 0 });

	bool active (lumex::block_hash const & hash) const;
	std::shared_ptr<lumex::election> election (lumex::block_hash const & hash) const;
	bool contains (lumex::block_hash const & hash) const;

	lumex::container_info container_info () const;

public: // Events
	using vote_processed_event_t = lumex::observer_set<std::shared_ptr<lumex::vote> const &, lumex::vote_source, std::unordered_map<lumex::block_hash, lumex::vote_code> const &>;
	vote_processed_event_t vote_processed;

private: // Dependencies
	lumex::vote_cache & vote_cache;
	lumex::recently_confirmed_cache & recently_confirmed;

private:
	void run ();

	// Mapping of block hashes to elections
	std::unordered_map<lumex::block_hash, std::weak_ptr<lumex::election>> elections;

	bool stopped{ false };
	mutable std::shared_mutex mutex;
	std::condition_variable_any condition;
	std::thread thread;
};
}
