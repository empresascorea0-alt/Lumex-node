#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/secure/common.hpp>

#include <memory>
#include <thread>
#include <unordered_set>

namespace lumex
{
class ledger;
class network_params;
class stats;
class logger;
class container_info_component;
class online_reps;

// Higher number means higher priority
enum class rep_tier
{
	none, // Not a principal representatives
	tier_1, // (0.1-1%) of online stake
	tier_2, // (1-5%) of online stake
	tier_3, // (> 5%) of online stake
};

lumex::stat::detail to_stat_detail (rep_tier);

class rep_tiers final
{
public:
	rep_tiers (lumex::ledger &, lumex::network_params &, lumex::online_reps &, lumex::stats &, lumex::logger &);
	~rep_tiers ();

	void start ();
	void stop ();

	/** Returns the representative tier for the account */
	lumex::rep_tier tier (lumex::account const & representative) const;

	lumex::container_info container_info () const;

private: // Dependencies
	lumex::ledger & ledger;
	lumex::network_params & network_params;
	lumex::online_reps & online_reps;
	lumex::stats & stats;
	lumex::logger & logger;

private:
	void run ();
	void calculate_tiers ();

private:
	/** Representatives levels for early prioritization */
	std::unordered_set<lumex::account> representatives_1;
	std::unordered_set<lumex::account> representatives_2;
	std::unordered_set<lumex::account> representatives_3;

	std::atomic<bool> stopped{ false };
	lumex::condition_variable condition;
	mutable lumex::mutex mutex;
	std::thread thread;
};
}
