#pragma once

#include <lumex/lib/interval.hpp>
#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <memory>
#include <thread>
#include <vector>

namespace mi = boost::multi_index;

namespace lumex
{
/** Track online representatives and trend online weight */
class online_reps final
{
public:
	online_reps (lumex::node_config const &, lumex::node &, lumex::ledger &, lumex::stats &, lumex::logger &);
	~online_reps ();

	void start ();
	void stop ();

	/** Add voting account \p rep_account to the set of online representatives */
	void observe (lumex::account const & rep_account);

	/** Returns the trended online stake */
	lumex::uint128_t trended () const;
	/** Returns the current online stake */
	lumex::uint128_t online () const;
	/** Returns the quorum required for confirmation*/
	lumex::uint128_t delta () const;
	/** List of online representatives, both the currently sampling ones and the ones observed in the previous sampling period */
	std::vector<lumex::account> list ();

	void clear ();

	lumex::container_info container_info () const;

public:
	// TODO: This should be in the network constants
	static unsigned constexpr online_weight_quorum = 67;

private: // Dependencies
	lumex::node_config const & config;
	lumex::node & node;
	lumex::ledger & ledger;
	lumex::stats & stats;
	lumex::logger & logger;

private:
	void run ();
	/** Called periodically to sample online weight */
	bool sample ();
	void trim ();
	/** Remove old records from the database */
	void trim_trended (lumex::store::write_transaction const &);
	/** Iterate over all database samples and remove invalid records. This is meant to clean potential leftovers from previous versions. */
	void sanitize_trended (lumex::store::write_transaction const &);
	void update_online ();

	struct trended_result
	{
		lumex::uint128_t trended;
		size_t samples;
	};
	trended_result calculate_trended (lumex::store::transaction const &) const;
	lumex::uint128_t calculate_online () const;

	bool verify_consistency (lumex::store::write_transaction const &, std::chrono::system_clock::time_point now, std::chrono::system_clock::time_point cutoff) const;

private:
	struct rep_info
	{
		std::chrono::steady_clock::time_point time;
		lumex::account account;
	};

	// clang-format off
	class tag_time {};
	class tag_account {};

	using ordered_reps = boost::multi_index_container<rep_info,
	mi::indexed_by<
		mi::ordered_non_unique<mi::tag<tag_time>,
			mi::member<rep_info, std::chrono::steady_clock::time_point, &rep_info::time>>,
		mi::hashed_unique<mi::tag<tag_account>,
			mi::member<rep_info, lumex::account, &rep_info::account>>
	>>;
	// clang-format off
	ordered_reps reps;

	lumex::uint128_t cached_trended{0};
	lumex::uint128_t cached_online{0};

	std::chrono::steady_clock::time_point last_sample;

	lumex::interval_mt low_weight_warning_interval;

	bool stopped{ false };
	lumex::condition_variable condition;
	mutable lumex::mutex mutex;
	std::thread thread;

public: // Only for tests
	void force_online_weight (lumex::uint128_t const & online_weight);
	void force_sample ();
};
}
