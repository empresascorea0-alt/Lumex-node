#include <lumex/lib/blocks.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/election.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/scheduler/bucket.hpp>
#include <lumex/node/scheduler/priority.hpp>

lumex::scheduler::bucket::bucket (lumex::bucket_index index_a, priority_config const & config_a, lumex::active_elections & active_a, lumex::stats & stats_a, lumex::logger & logger_a) :
	index{ index_a },
	config{ config_a },
	active{ active_a },
	stats{ stats_a },
	logger{ logger_a }
{
}

lumex::scheduler::bucket::~bucket ()
{
}

bool lumex::scheduler::bucket::available (priority_entry top) const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };

	if (!top.block)
	{
		return false; // No block available
	}
	else
	{
		return activate_predicate (top.priority);
	}
}

bool lumex::scheduler::bucket::activate_predicate (lumex::priority_timestamp candidate) const
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
		return active.vacancy (lumex::election_behavior::priority) > 0;
	}

	// We are at max election capacity, only activate if candidate has better priority than the worst priority election
	if (!elections.empty ())
	{
		auto & by_priority = elections.get<tag_priority> ();
		auto worst_it = by_priority.begin (); // Worst priority (highest priority value)
		release_assert (worst_it != by_priority.end ());

		// Lower priority value is better
		if (candidate < worst_it->priority)
		{
			// Bound number of reprioritizations up to twice the max election capacity
			return elections.size () < config.max_elections * 2;
		};
	}

	return false;
}

bool lumex::scheduler::bucket::overfill_predicate () const
{
	debug_assert (!mutex.try_lock ());

	if (elections.size () <= config.reserved_elections)
	{
		// Never overfill within reserved capacity
		return false;
	}
	if (elections.size () <= config.max_elections)
	{
		// Only overfill above reserved capacity if there is no vacancy in active elections
		return active.vacancy (lumex::election_behavior::priority) < 0;
	}

	return true; // Overfill since we are at or above max election capacity
}

bool lumex::scheduler::bucket::activate (priority_entry top)
{
	debug_assert (top.block);
	debug_assert (top.bucket == index);

	lumex::lock_guard<lumex::mutex> lock{ mutex };

	auto erase_callback = [this] (std::shared_ptr<lumex::election> election) {
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		elections.get<tag_root> ().erase (election->qualified_root);
	};

	auto result = active.insert (top.block, lumex::election_behavior::priority, index, top.priority, erase_callback);
	if (result.inserted)
	{
		release_assert (result.election);
		elections.get<tag_root> ().insert ({ result.election, result.election->qualified_root, top.priority });

		stats.inc (lumex::stat::type::election_bucket, lumex::stat::detail::activate_success);

		logger.debug (lumex::log::type::election_scheduler,
		"Inserted election for block: {}, root: {} (account: {}, bucket: {}, priority timestamp: {})",
		top.block->hash (),
		top.block->qualified_root (),
		top.block->account (),
		index,
		top.priority);
	}
	else
	{
		stats.inc (lumex::stat::type::election_bucket, lumex::stat::detail::activate_failed);
	}

	return result.inserted;
}

bool lumex::scheduler::bucket::cleanup ()
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };

	if (overfill_predicate ())
	{
		return cancel_lowest_election ();
	}

	return false; // Nothing cancelled
}

size_t lumex::scheduler::bucket::election_count () const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	return elections.size ();
}

bool lumex::scheduler::bucket::cancel_lowest_election ()
{
	debug_assert (!mutex.try_lock ());

	if (!elections.empty ())
	{
		auto & by_priority = elections.get<tag_priority> ();
		auto worst_it = by_priority.begin (); // Worst priority (highest priority value)
		release_assert (worst_it != by_priority.end ());

		auto election = worst_it->election;

		logger.debug (lumex::log::type::election_scheduler,
		"Cancelling lowest-priority election for root: {} (account: {}, bucket: {}, priority timestamp: {})",
		election->qualified_root,
		election->account,
		index,
		worst_it->priority);

		stats.inc (lumex::stat::type::election_bucket, lumex::stat::detail::cancel_lowest);

		election->cancel ();

		return true; // Cancelled
	}

	return false;
}