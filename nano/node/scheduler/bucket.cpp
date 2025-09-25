#include <nano/lib/blocks.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/election.hpp>
#include <nano/node/node.hpp>
#include <nano/node/scheduler/bucket.hpp>

nano::scheduler::bucket::bucket (nano::bucket_index index_a, priority_config const & config_a, nano::active_elections & active_a, nano::stats & stats_a) :
	index{ index_a },
	config{ config_a },
	active{ active_a },
	stats{ stats_a }
{
}

nano::scheduler::bucket::~bucket ()
{
}

bool nano::scheduler::bucket::available (priority_entry top) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	if (!top.block)
	{
		return false; // No block available
	}
	else
	{
		return activate_predicate (top.priority);
	}
}

bool nano::scheduler::bucket::activate_predicate (nano::priority_timestamp candidate) const
{
	debug_assert (!mutex.try_lock ());

	if (elections.size () < config.reserved_elections)
	{
		// Always activate within reserved capacity
		return true;
	}
	if (elections.size () < config.max_elections)
	{
		// Only activate above reserved capacity if there is vacancy in active elections
		return active.vacancy (nano::election_behavior::priority) > 0;
	}

	// We are at max election capacity, only activate if candidate has better priority than the worst priority election
	if (!elections.empty ())
	{
		auto lowest = elections.get<tag_priority> ().begin ()->priority;

		// Compare to equal to drain duplicates
		if (candidate <= lowest)
		{
			// Bound number of reprioritizations up to twice the max election capacity
			return elections.size () < config.max_elections * 2;
		};
	}

	return false;
}

bool nano::scheduler::bucket::overfill_predicate () const
{
	debug_assert (!mutex.try_lock ());

	if (elections.size () < config.reserved_elections)
	{
		// Never overfill within reserved capacity
		return false;
	}
	if (elections.size () <= config.max_elections)
	{
		// Only overfill above reserved capacity if there is no vacancy in active elections
		return active.vacancy (nano::election_behavior::priority) < 0;
	}

	return true; // Overfill since we are at or above max election capacity
}

bool nano::scheduler::bucket::activate (priority_entry top)
{
	debug_assert (top.block);
	debug_assert (top.bucket == index);

	nano::lock_guard<nano::mutex> lock{ mutex };

	auto erase_callback = [this] (std::shared_ptr<nano::election> election) {
		nano::lock_guard<nano::mutex> lock{ mutex };
		elections.get<tag_root> ().erase (election->qualified_root);
	};

	auto result = active.insert (top.block, nano::election_behavior::priority, index, top.priority, erase_callback);
	if (result.inserted)
	{
		release_assert (result.election);
		elections.get<tag_root> ().insert ({ result.election, result.election->qualified_root, top.priority });

		stats.inc (nano::stat::type::election_bucket, nano::stat::detail::activate_success);
	}
	else
	{
		stats.inc (nano::stat::type::election_bucket, nano::stat::detail::activate_failed);
	}

	return result.inserted;
}

bool nano::scheduler::bucket::cleanup ()
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	if (overfill_predicate ())
	{
		return cancel_lowest_election ();
	}

	return false; // Nothing cancelled
}

size_t nano::scheduler::bucket::election_count () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return elections.size ();
}

bool nano::scheduler::bucket::cancel_lowest_election ()
{
	debug_assert (!mutex.try_lock ());

	if (!elections.empty ())
	{
		elections.get<tag_priority> ().begin ()->election->cancel ();

		stats.inc (nano::stat::type::election_bucket, nano::stat::detail::cancel_lowest);

		return true; // Cancelled
	}

	return false;
}