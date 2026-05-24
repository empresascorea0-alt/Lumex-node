#pragma once

#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/store/transaction.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <thread>

namespace mi = boost::multi_index;

namespace lumex::scheduler
{
class hinted_config final
{
public:
	explicit hinted_config (lumex::network_constants const &);

	lumex::error deserialize (lumex::tomlconfig & toml);
	lumex::error serialize (lumex::tomlconfig & toml) const;

public:
	bool enable{ true };
	std::chrono::milliseconds check_interval{ 1000 };
	std::chrono::milliseconds block_cooldown{ 10000 };
	unsigned hinting_threshold_percent{ 10 };
	unsigned vacancy_threshold_percent{ 20 };
};

/*
 * Monitors inactive vote cache and schedules elections with the highest observed vote tally.
 */
class hinted final
{
public:
	hinted (hinted_config const &, lumex::node &, lumex::vote_cache &, lumex::active_elections &, lumex::online_reps &, lumex::stats &);
	~hinted ();

	void start ();
	void stop ();

	/*
	 * Notify about changes in AEC vacancy
	 */
	void notify ();

	lumex::container_info container_info () const;

private:
	bool predicate () const;
	void run ();
	void run_iterative ();
	void activate (secure::read_transaction &, lumex::block_hash const & hash, bool check_dependencies);

	lumex::uint128_t tally_threshold () const;
	lumex::uint128_t final_tally_threshold () const;

private: // Dependencies
	hinted_config const & config;
	lumex::node & node;
	lumex::vote_cache & vote_cache;
	lumex::active_elections & active;
	lumex::online_reps & online_reps;
	lumex::stats & stats;

private:
	std::atomic<bool> stopped{ false };
	lumex::condition_variable condition;
	mutable lumex::mutex mutex;
	std::thread thread;

private:
	bool cooldown (lumex::block_hash const & hash);

	struct cooldown_entry
	{
		lumex::block_hash hash;
		std::chrono::steady_clock::time_point timeout;
	};

	// clang-format off
	class tag_hash {};
	class tag_timeout {};
	// clang-format on

	// clang-format off
	using ordered_cooldowns = boost::multi_index_container<cooldown_entry,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<cooldown_entry, lumex::block_hash, &cooldown_entry::hash>>,
		mi::ordered_non_unique<mi::tag<tag_timeout>,
			mi::member<cooldown_entry, std::chrono::steady_clock::time_point, &cooldown_entry::timeout>>
	>>;
	// clang-format on

	ordered_cooldowns cooldowns_m;
};
}
