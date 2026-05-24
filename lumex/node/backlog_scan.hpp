#pragma once

#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/observer_set.hpp>
#include <lumex/lib/rate_limiting.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/secure/account_info.hpp>
#include <lumex/secure/common.hpp>

#include <condition_variable>
#include <deque>
#include <thread>

namespace lumex
{
class backlog_scan_config final
{
public:
	lumex::error deserialize (lumex::tomlconfig &);
	lumex::error serialize (lumex::tomlconfig &) const;

public:
	/** Control if ongoing backlog population is enabled. If not, backlog population can still be triggered by RPC */
	bool enable{ true };
	/** Number of accounts to scan per second. */
	size_t rate_limit{ 10000 };
	/** Number of accounts per second to process. */
	size_t batch_size{ 1000 };
};

class backlog_scan final
{
public:
	backlog_scan (backlog_scan_config const &, lumex::ledger &, lumex::stats &);
	~backlog_scan ();

	void start ();
	void stop ();

	/** Manually trigger backlog population */
	void trigger ();

	/** Notify about AEC vacancy */
	void notify ();

	lumex::container_info container_info () const;

public:
	struct activated_info
	{
		lumex::account account;
		lumex::account_info account_info;
		lumex::confirmation_height_info conf_info;
	};

	using batch_event_t = lumex::observer_set<std::deque<activated_info>>;
	batch_event_t batch_scanned; // Accounts scanned but not activated
	batch_event_t batch_activated; // Accounts activated

private: // Dependencies
	backlog_scan_config const & config;
	lumex::ledger & ledger;
	lumex::stats & stats;

private:
	void run ();
	bool predicate () const;
	void populate_backlog (lumex::unique_lock<lumex::mutex> & lock);

private:
	lumex::rate_limiter limiter;

	/** This is a manual trigger, the ongoing backlog population does not use this.
	 *  It can be triggered even when backlog population (frontiers confirmation) is disabled. */
	bool triggered{ false };

	bool stopped{ false };
	lumex::condition_variable condition;
	mutable lumex::mutex mutex;

	/** Thread that runs the backlog implementation logic. The thread always runs, even if
	 *  backlog population is disabled, so that it can service a manual trigger (e.g. via RPC). */
	std::thread thread;
};
}
