#pragma once

#include <lumex/lib/locks.hpp>

#include <chrono>

using namespace std::chrono_literals;

#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/lib/timer.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <condition_variable>
#include <memory>
#include <queue>
#include <thread>
#include <vector>

namespace mi = boost::multi_index;

namespace lumex::scheduler
{
class optimistic_config final
{
public:
	lumex::error deserialize (lumex::tomlconfig & toml);
	lumex::error serialize (lumex::tomlconfig & toml) const;

public:
	bool enable{ true };

	/** Minimum difference between confirmation frontier and account frontier to become a candidate for optimistic confirmation */
	uint64_t gap_threshold{ 16 };

	/** Maximum number of candidates stored in memory */
	std::size_t max_size{ 1024 * 4 };

	/** How much to delay activation of optimistic elections to avoid interfering with election scheduler */
	std::chrono::milliseconds activation_delay{ 1s };
};

class optimistic final
{
	struct entry;

public:
	optimistic (optimistic_config const &, lumex::node &, lumex::ledger &, lumex::active_elections &, lumex::network_constants const & network_constants, lumex::stats &);
	~optimistic ();

	void start ();
	void stop ();

	/**
	 * Called from backlog population to process accounts with unconfirmed blocks
	 */
	bool activate (lumex::account const &, lumex::account_info const &, lumex::confirmation_height_info const &);

	/**
	 * Notify about changes in AEC vacancy
	 */
	void notify ();

	lumex::container_info container_info () const;

private:
	bool activate_predicate (lumex::account_info const &, lumex::confirmation_height_info const &) const;

	bool predicate () const;
	void run ();
	void run_iterative (lumex::unique_lock<lumex::mutex> &);
	bool run_one (secure::transaction const &, entry const & candidate);

	std::deque<entry> snapshot (size_t max_count) const;

private: // Dependencies
	optimistic_config const & config;
	lumex::node & node;
	lumex::ledger & ledger;
	lumex::active_elections & active;
	lumex::network_constants const & network_constants;
	lumex::stats & stats;

private:
	struct entry
	{
		lumex::account account;
		uint64_t unconfirmed_height;
		std::chrono::steady_clock::time_point timestamp;
	};

	// clang-format off
	class tag_sequenced {};
	class tag_account {};
	class tag_unconfirmed_height {};

	using ordered_candidates = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_account>,
			mi::member<entry, lumex::account, &entry::account>>,
		mi::ordered_non_unique<mi::tag<tag_unconfirmed_height>,
			mi::member<entry, uint64_t, &entry::unconfirmed_height>, std::greater<>> // Descending
	>>;
	// clang-format on

	/** Accounts eligible for optimistic scheduling */
	ordered_candidates candidates;

	std::atomic<bool> stopped{ false };
	lumex::condition_variable condition;
	mutable lumex::mutex mutex;
	std::thread thread;
};
}
