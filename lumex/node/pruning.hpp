#pragma once

#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/node/fwd.hpp>

#include <condition_variable>
#include <deque>
#include <thread>

namespace lumex
{
class pruning final
{
public:
	pruning (lumex::node_config const &, lumex::node_flags const &, lumex::ledger &, lumex::stats &, lumex::logger &);
	~pruning ();

	void start ();
	void stop ();

	lumex::container_info container_info () const;

	void ledger_pruning (uint64_t batch_size, bool bootstrap_weight_reached);
	bool collect_ledger_pruning_targets (std::deque<lumex::block_hash> & pruning_targets_out, lumex::account & last_account_out, uint64_t batch_read_size, uint64_t max_depth, uint64_t cutoff_time);

private: // Dependencies
	lumex::node_config const & config;
	lumex::node_flags const & flags;
	lumex::ledger & ledger;
	lumex::stats & stats;
	lumex::logger & logger;

private:
	void run ();

private:
	bool stopped{ false };
	lumex::condition_variable condition;
	mutable lumex::mutex mutex;
	std::thread thread;
};
}