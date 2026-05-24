#include <lumex/lib/blocks.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/election.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/scheduler/manual.hpp>

lumex::scheduler::manual::manual (lumex::node & node) :
	node{ node }
{
}

lumex::scheduler::manual::~manual ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void lumex::scheduler::manual::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		lumex::thread_role::set (lumex::thread_role::name::scheduler_manual);
		run ();
	} };
}

void lumex::scheduler::manual::stop ()
{
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	lumex::join_or_pass (thread);
}

void lumex::scheduler::manual::notify ()
{
	condition.notify_all ();
}

auto lumex::scheduler::manual::push (std::shared_ptr<lumex::block> const & block) -> std::future<std::shared_ptr<lumex::election>>
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };

	// Check if block already exists
	auto & hash_index = queue.get<tag_hash> ();

	if (hash_index.contains (block->hash ()))
	{
		// Block already exists, return future that immediately resolves to nullptr
		std::promise<std::shared_ptr<lumex::election>> promise;
		auto future = promise.get_future ();
		promise.set_value (nullptr);
		return future;
	}

	// Create entry and get future before inserting
	entry new_entry{ block };
	auto future = new_entry.promise.get_future ();

	auto [it, inserted] = queue.push_back (std::move (new_entry));
	debug_assert (inserted);

	condition.notify_all ();
	return future;
}

bool lumex::scheduler::manual::contains (lumex::block_hash const & hash) const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	auto & hash_index = queue.get<tag_hash> ();
	return hash_index.contains (hash);
}

bool lumex::scheduler::manual::predicate () const
{
	debug_assert (!mutex.try_lock ());
	return !queue.empty ();
}

void lumex::scheduler::manual::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait (lock, [this] () {
			return stopped || predicate ();
		});

		if (stopped)
		{
			return;
		}

		debug_assert ((std::this_thread::yield (), true));

		if (predicate ())
		{
			node.stats.inc (lumex::stat::type::election_scheduler, lumex::stat::detail::loop);

			auto promise = std::move (queue.front ().promise);
			auto block = queue.front ().block;
			queue.pop_front ();

			lock.unlock ();

			auto result = node.active.insert (block, lumex::election_behavior::manual);
			if (result.inserted)
			{
				node.stats.inc (lumex::stat::type::election_scheduler, lumex::stat::detail::insert_manual);
			}
			if (result.election != nullptr)
			{
				result.election->transition_active ();
			}

			// Fulfill the promise
			promise.set_value (result.election);

			lock.lock ();
		}
	}
}

lumex::container_info lumex::scheduler::manual::container_info () const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };

	lumex::container_info info;
	info.put ("queue", queue);
	return info;
}