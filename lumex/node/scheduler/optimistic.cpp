#include <lumex/lib/blocks.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/tomlconfig.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/election_behavior.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/scheduler/optimistic.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_set_any.hpp>
#include <lumex/secure/ledger_set_cemented.hpp>

lumex::scheduler::optimistic::optimistic (optimistic_config const & config_a, lumex::node & node_a, lumex::ledger & ledger_a, lumex::active_elections & active_a, lumex::network_constants const & network_constants_a, lumex::stats & stats_a) :
	config{ config_a },
	node{ node_a },
	ledger{ ledger_a },
	active{ active_a },
	network_constants{ network_constants_a },
	stats{ stats_a }
{
}

lumex::scheduler::optimistic::~optimistic ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void lumex::scheduler::optimistic::start ()
{
	debug_assert (!thread.joinable ());

	if (!config.enable)
	{
		return;
	}

	thread = std::thread{ [this] () {
		lumex::thread_role::set (lumex::thread_role::name::scheduler_optimistic);
		run ();
	} };
}

void lumex::scheduler::optimistic::stop ()
{
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	join_or_pass (thread);
}

void lumex::scheduler::optimistic::notify ()
{
	// Only wake up the thread if there is space inside AEC for optimistic elections
	if (active.vacancy (lumex::election_behavior::optimistic) > 0)
	{
		condition.notify_all ();
	}
}

bool lumex::scheduler::optimistic::activate_predicate (const lumex::account_info & account_info, const lumex::confirmation_height_info & conf_info) const
{
	// Chain with a big enough gap between account frontier and confirmation frontier
	if (account_info.block_count - conf_info.height > config.gap_threshold)
	{
		return true;
	}
	// Account with nothing confirmed yet
	if (conf_info.height == 0)
	{
		return true;
	}
	return false;
}

bool lumex::scheduler::optimistic::activate (const lumex::account & account, const lumex::account_info & account_info, const lumex::confirmation_height_info & conf_info)
{
	debug_assert (account_info.block_count >= conf_info.height);

	if (!config.enable)
	{
		return false;
	}

	if (activate_predicate (account_info, conf_info))
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };

		// Assume gap_threshold for accounts with nothing confirmed
		auto const unconfirmed_height = std::max (account_info.block_count - conf_info.height, config.gap_threshold);

		auto [it, inserted] = candidates.push_back ({ account, unconfirmed_height, std::chrono::steady_clock::now () });

		stats.inc (lumex::stat::type::optimistic_scheduler, inserted ? lumex::stat::detail::activated : lumex::stat::detail::duplicate);

		// Limit candidates container size
		if (candidates.size () > config.max_size)
		{
			// Remove oldest candidate
			candidates.pop_front ();
		}

		// Not notifying the thread immediately here, since we need to wait for activation_delay to elapse

		return inserted;
	}

	return false; // Not activated
}

bool lumex::scheduler::optimistic::predicate () const
{
	debug_assert (!mutex.try_lock ());

	// Check if there is space inside AEC for a new optimistic election
	return !candidates.empty () && active.vacancy (lumex::election_behavior::optimistic) > 0;
}

auto lumex::scheduler::optimistic::snapshot (size_t max_count) const -> std::deque<entry>
{
	auto const now = std::chrono::steady_clock::now ();

	std::deque<entry> result;

	auto & height_index = candidates.get<tag_unconfirmed_height> ();
	for (auto it = height_index.begin (); it != height_index.end () && result.size () < max_count; ++it)
	{
		if (elapsed (it->timestamp, config.activation_delay, now))
		{
			result.push_back (*it);
		}
	}

	return result;
}

void lumex::scheduler::optimistic::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		// Ignore predicate in condition, we always want to wait for activation_delay to elapse before next wake up
		condition.wait_for (lock, config.activation_delay, [this] () {
			return stopped.load ();
		});

		if (stopped)
		{
			return;
		}

		if (predicate ())
		{
			stats.inc (lumex::stat::type::optimistic_scheduler, lumex::stat::detail::loop);

			run_iterative (lock);
			debug_assert (!lock.owns_lock ());
			lock.lock ();
		}
	}
}

void lumex::scheduler::optimistic::run_iterative (lumex::unique_lock<lumex::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());

	auto tops = snapshot (active.limit (lumex::election_behavior::optimistic));

	lock.unlock ();

	auto transaction = ledger.tx_begin_read ();

	for (auto const & candidate : tops)
	{
		if (stopped)
		{
			return;
		}

		transaction.refresh_if_needed ();

		bool good = run_one (transaction, candidate);
		if (!good)
		{
			// Remove no longer valid candidate
			lumex::lock_guard<lumex::mutex> guard{ mutex };
			auto & account_index = candidates.get<tag_account> ();
			account_index.erase (candidate.account);
			stats.inc (lumex::stat::type::optimistic_scheduler, lumex::stat::detail::erased);
		}
	}
}

bool lumex::scheduler::optimistic::run_one (secure::transaction const & transaction, entry const & candidate)
{
	auto block = ledger.any.block_get (transaction, ledger.any.account_head (transaction, candidate.account));
	if (block)
	{
		// Ensure block is not already confirmed
		if (!node.block_confirmed_or_being_confirmed (transaction, block->hash ()))
		{
			// Try to insert it into AEC
			auto result = node.active.insert (block, lumex::election_behavior::optimistic);
			stats.inc (lumex::stat::type::optimistic_scheduler, result.inserted ? lumex::stat::detail::insert : lumex::stat::detail::insert_failed);

			return true; // Activation attempted
		}
	}
	return false; // Activation not attempted, block not found or already confirmed, should be erased from candidates
}

lumex::container_info lumex::scheduler::optimistic::container_info () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	lumex::container_info info;
	info.put ("candidates", candidates);
	return info;
}

/*
 * optimistic_scheduler_config
 */

lumex::error lumex::scheduler::optimistic_config::deserialize (lumex::tomlconfig & toml)
{
	toml.get ("enable", enable);
	toml.get ("gap_threshold", gap_threshold);
	toml.get ("max_size", max_size);
	toml.get_duration ("activation_delay", activation_delay);

	return toml.get_error ();
}

lumex::error lumex::scheduler::optimistic_config::serialize (lumex::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Enable or disable optimistic elections\ntype:bool");
	toml.put ("gap_threshold", gap_threshold, "Minimum difference between confirmation frontier and account frontier to become a candidate for optimistic confirmation\ntype:uint64");
	toml.put ("max_size", max_size, "Maximum number of candidates stored in memory\ntype:uint64");
	toml.put ("activation_delay", activation_delay.count (), "How much to delay activation of optimistic elections to avoid interfering with election scheduler\ntype:milliseconds");

	return toml.get_error ();
}
