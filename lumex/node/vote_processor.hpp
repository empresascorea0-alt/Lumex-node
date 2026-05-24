#pragma once

#include <lumex/lib/interval.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/threading.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/node/fair_queue_traits.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/node/rep_tiers.hpp>
#include <lumex/node/vote_router.hpp>
#include <lumex/secure/common.hpp>

#include <deque>
#include <memory>
#include <thread>
#include <unordered_set>

namespace lumex
{
class vote_processor_config final
{
public:
	lumex::error serialize (lumex::tomlconfig & toml) const;
	lumex::error deserialize (lumex::tomlconfig & toml);

public:
	bool enable{ true };

	size_t max_pr_queue{ 256 };
	size_t max_non_pr_queue{ 32 };
	size_t pr_priority{ 3 };
	size_t threads{ std::clamp (lumex::hardware_concurrency () / 2, 1u, 4u) };
	size_t batch_size{ 1024 };
	size_t max_triggered{ 16384 };
};

class vote_processor final
{
public:
	vote_processor (vote_processor_config const &, lumex::vote_router &, lumex::node_observers &, lumex::stats &, lumex::node_flags &, lumex::logger &, lumex::online_reps &, lumex::rep_crawler &, lumex::ledger &, lumex::network_params &, lumex::rep_tiers &);
	~vote_processor ();

	void start ();
	void stop ();

	/** Queue vote for processing. @returns true if the vote was queued */
	bool vote (std::shared_ptr<lumex::vote> const &, std::shared_ptr<lumex::transport::channel> const &, lumex::vote_source = lumex::vote_source::live);
	lumex::vote_code vote_blocking (std::shared_ptr<lumex::vote> const &, std::shared_ptr<lumex::transport::channel> const &, lumex::vote_source = lumex::vote_source::live);

	/** Queue hash for vote cache lookup and processing. */
	void trigger (lumex::block_hash const & hash);

	std::size_t size () const;
	bool empty () const;

	lumex::container_info container_info () const;

	std::atomic<uint64_t> total_processed{ 0 };

private: // Dependencies
	vote_processor_config const & config;
	lumex::vote_router & vote_router;
	lumex::node_observers & observers;
	lumex::stats & stats;
	lumex::logger & logger;
	lumex::online_reps & online_reps;
	lumex::rep_crawler & rep_crawler;
	lumex::ledger & ledger;
	lumex::network_params & network_params;
	lumex::rep_tiers & rep_tiers;

private:
	void run ();
	void run_batch (lumex::unique_lock<lumex::mutex> &);

private:
	using entry_t = std::pair<std::shared_ptr<lumex::vote>, lumex::vote_source>;
	lumex::fair_queue<entry_t, lumex::rep_tier, std::shared_ptr<lumex::transport::channel>> queue;

private:
	bool stopped{ false };
	lumex::condition_variable condition;
	mutable lumex::mutex mutex{ mutex_identifier (mutexes::vote_processor) };
	std::vector<std::thread> threads;

	lumex::interval log_interval;
};

class vote_cache_processor final
{
public:
	vote_cache_processor (vote_processor_config const &, lumex::vote_router &, lumex::vote_cache &, lumex::stats &, lumex::logger &);
	~vote_cache_processor ();

	void start ();
	void stop ();

	/** Queue hash for vote cache lookup and processing. */
	void trigger (lumex::block_hash const & hash);

	std::size_t size () const;
	bool empty () const;

	lumex::container_info container_info () const;

private:
	void run ();
	void run_batch (lumex::unique_lock<lumex::mutex> &);

private: // Dependencies
	vote_processor_config const & config;
	lumex::vote_router & vote_router;
	lumex::vote_cache & vote_cache;
	lumex::stats & stats;
	lumex::logger & logger;

private:
	std::deque<lumex::block_hash> triggered;

	bool stopped{ false };
	lumex::condition_variable condition;
	mutable lumex::mutex mutex;
	std::thread thread;

	lumex::interval log_interval;
};
}
