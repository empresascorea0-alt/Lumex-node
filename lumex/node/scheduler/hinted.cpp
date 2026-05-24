#include <lumex/lib/blocks.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/tomlconfig.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/election_behavior.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/online_reps.hpp>
#include <lumex/node/scheduler/hinted.hpp>
#include <lumex/node/vote_cache.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_set_any.hpp>

#include <stack>

lumex::scheduler::hinted::hinted (hinted_config const & config_a, lumex::node & node_a, lumex::vote_cache & vote_cache_a, lumex::active_elections & active_a, lumex::online_reps & online_reps_a, lumex::stats & stats_a) :
	config{ config_a },
	node{ node_a },
	vote_cache{ vote_cache_a },
	active{ active_a },
	online_reps{ online_reps_a },
	stats{ stats_a }
{
}

lumex::scheduler::hinted::~hinted ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void lumex::scheduler::hinted::start ()
{
	debug_assert (!thread.joinable ());

	if (!config.enable)
	{
		return;
	}

	thread = std::thread{ [this] () {
		lumex::thread_role::set (lumex::thread_role::name::scheduler_hinted);
		run ();
	} };
}

void lumex::scheduler::hinted::stop ()
{
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	join_or_pass (thread);
}

void lumex::scheduler::hinted::notify ()
{
	// Avoid notifying when there is very little space inside AEC
	auto const limit = active.limit (lumex::election_behavior::hinted);
	if (active.vacancy (lumex::election_behavior::hinted) >= (limit * config.vacancy_threshold_percent / 100))
	{
		condition.notify_all ();
	}
}

bool lumex::scheduler::hinted::predicate () const
{
	// Check if there is space inside AEC for a new hinted election
	return active.vacancy (lumex::election_behavior::hinted) > 0;
}

void lumex::scheduler::hinted::activate (secure::read_transaction & transaction, lumex::block_hash const & hash, bool check_dependencies)
{
	const int max_iterations = 64;

	std::set<lumex::block_hash> visited;
	std::stack<lumex::block_hash> stack;
	stack.push (hash);

	int iterations = 0;
	while (!stack.empty () && iterations++ < max_iterations)
	{
		transaction.refresh_if_needed ();

		const lumex::block_hash current_hash = stack.top ();
		stack.pop ();

		// Check if block exists
		if (auto block = node.ledger.any.block_get (transaction, current_hash); block)
		{
			// Ensure block is not already confirmed
			if (node.block_confirmed_or_being_confirmed (transaction, current_hash))
			{
				stats.inc (lumex::stat::type::hinting, lumex::stat::detail::already_confirmed);
				vote_cache.erase (current_hash); // Remove from vote cache
				continue; // Move on to the next item in the stack
			}

			if (check_dependencies)
			{
				// Perform a depth-first search of the dependency graph
				if (!node.ledger.dependencies_cemented (transaction, *block))
				{
					stats.inc (lumex::stat::type::hinting, lumex::stat::detail::dependency_unconfirmed);
					auto dependencies = block->dependencies ();
					for (const auto & dependency_hash : dependencies)
					{
						if (!dependency_hash.is_zero () && visited.insert (dependency_hash).second) // Avoid visiting the same block twice
						{
							stack.push (dependency_hash); // Add dependency block to the stack
						}
					}
					continue; // Move on to the next item in the stack
				}
			}

			// Try to insert it into AEC as hinted election
			auto result = node.active.insert (block, lumex::election_behavior::hinted);
			stats.inc (lumex::stat::type::hinting, result.inserted ? lumex::stat::detail::insert : lumex::stat::detail::insert_failed);
		}
		else
		{
			stats.inc (lumex::stat::type::hinting, lumex::stat::detail::missing_block);

			// TODO: Block is missing, bootstrap it
		}
	}
}

void lumex::scheduler::hinted::run_iterative ()
{
	const auto minimum_tally = tally_threshold ();
	const auto minimum_final_tally = final_tally_threshold ();

	// Get the list before db transaction starts to avoid unnecessary slowdowns
	auto tops = vote_cache.top (minimum_tally);

	auto transaction = node.ledger.tx_begin_read ();

	for (auto const & entry : tops)
	{
		if (stopped)
		{
			return;
		}

		if (!predicate ())
		{
			return;
		}

		if (cooldown (entry.hash))
		{
			continue;
		}

		// Check dependencies only if cached tally is lower than quorum
		if (entry.final_tally < minimum_final_tally)
		{
			// Ensure all dependency blocks are already confirmed before activating
			stats.inc (lumex::stat::type::hinting, lumex::stat::detail::activate);
			activate (transaction, entry.hash, /* check dependencies */ true);
		}
		else
		{
			// Blocks with a vote tally higher than quorum, can be activated and confirmed immediately
			stats.inc (lumex::stat::type::hinting, lumex::stat::detail::activate_immediate);
			activate (transaction, entry.hash, false);
		}
	}
}

void lumex::scheduler::hinted::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (lumex::stat::type::hinting, lumex::stat::detail::loop);

		condition.wait_for (lock, config.check_interval);

		debug_assert ((std::this_thread::yield (), true)); // Introduce some random delay in debug builds

		if (!stopped)
		{
			lock.unlock ();

			if (predicate ())
			{
				run_iterative ();
			}

			lock.lock ();
		}
	}
}

lumex::uint128_t lumex::scheduler::hinted::tally_threshold () const
{
	auto min_tally = (online_reps.trended () / 100) * config.hinting_threshold_percent;
	return min_tally;
}

lumex::uint128_t lumex::scheduler::hinted::final_tally_threshold () const
{
	auto quorum = online_reps.delta ();
	return quorum;
}

bool lumex::scheduler::hinted::cooldown (const lumex::block_hash & hash)
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	auto const now = std::chrono::steady_clock::now ();

	// Check if the hash is still in the cooldown period using the hashed index
	auto const & hashed_index = cooldowns_m.get<tag_hash> ();
	if (auto it = hashed_index.find (hash); it != hashed_index.end ())
	{
		if (it->timeout > now)
		{
			return true; // Needs cooldown
		}
		cooldowns_m.erase (it); // Entry is outdated, so remove it
	}

	// Insert the new entry
	cooldowns_m.insert ({ hash, now + config.block_cooldown });

	// Trim old entries
	auto & seq_index = cooldowns_m.get<tag_timeout> ();
	while (!seq_index.empty () && seq_index.begin ()->timeout <= now)
	{
		seq_index.erase (seq_index.begin ());
	}

	return false; // No need to cooldown
}

lumex::container_info lumex::scheduler::hinted::container_info () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	lumex::container_info info;
	info.put ("cooldowns", cooldowns_m);
	return info;
}

/*
 * hinted_config
 */

lumex::scheduler::hinted_config::hinted_config (lumex::network_constants const & network)
{
	if (network.is_dev_network ())
	{
		check_interval = std::chrono::milliseconds{ 100 };
		block_cooldown = std::chrono::milliseconds{ 100 };
	}
}

lumex::error lumex::scheduler::hinted_config::serialize (lumex::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Enable or disable hinted elections\ntype:bool");
	toml.put ("hinting_threshold", hinting_threshold_percent, "Percentage of online weight needed to start a hinted election. \ntype:uint32,[0,100]");
	toml.put ("check_interval", check_interval.count (), "Interval between scans of the vote cache for possible hinted elections. \ntype:milliseconds");
	toml.put ("block_cooldown", block_cooldown.count (), "Cooldown period for blocks that failed to start an election. \ntype:milliseconds");
	toml.put ("vacancy_threshold", vacancy_threshold_percent, "Percentage of available space in the active elections container needed to trigger a scan for hinted elections (before the check interval elapses). \ntype:uint32,[0,100]");

	return toml.get_error ();
}

lumex::error lumex::scheduler::hinted_config::deserialize (lumex::tomlconfig & toml)
{
	toml.get ("enable", enable);
	toml.get ("hinting_threshold", hinting_threshold_percent);

	toml.get_duration ("check_interval", check_interval);
	toml.get_duration ("block_cooldown", block_cooldown);

	toml.get ("vacancy_threshold", vacancy_threshold_percent);

	if (hinting_threshold_percent > 100)
	{
		toml.get_error ().set ("hinting_threshold must be a number between 0 and 100");
	}
	if (vacancy_threshold_percent > 100)
	{
		toml.get_error ().set ("vacancy_threshold must be a number between 0 and 100");
	}

	return toml.get_error ();
}
