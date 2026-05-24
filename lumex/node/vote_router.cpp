#include <lumex/lib/enum_util.hpp>
#include <lumex/lib/thread_roles.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/lib/vote.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/election.hpp>
#include <lumex/node/vote_cache.hpp>
#include <lumex/node/vote_router.hpp>

#include <chrono>

using namespace std::chrono_literals;

lumex::vote_router::vote_router (lumex::vote_cache & vote_cache_a, lumex::recently_confirmed_cache & recently_confirmed_a) :
	vote_cache{ vote_cache_a },
	recently_confirmed{ recently_confirmed_a }
{
}

lumex::vote_router::~vote_router ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void lumex::vote_router::start ()
{
	thread = std::thread{ [this] () {
		lumex::thread_role::set (lumex::thread_role::name::vote_router);
		run ();
	} };
}

void lumex::vote_router::stop ()
{
	{
		std::unique_lock lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

void lumex::vote_router::connect (lumex::block_hash const & hash, std::weak_ptr<lumex::election> election)
{
	std::unique_lock lock{ mutex };
	elections.insert_or_assign (hash, election);
}

std::size_t lumex::vote_router::disconnect (lumex::election const & election)
{
	std::unique_lock lock{ mutex };
	std::size_t erased = 0;
	for (auto const & [hash, _] : election.blocks ())
	{
		erased += elections.erase (hash);
	}
	return erased;
}

bool lumex::vote_router::disconnect (lumex::block_hash const & hash)
{
	std::unique_lock lock{ mutex };
	auto erased = elections.erase (hash);
	return erased > 0;
}

std::unordered_map<lumex::block_hash, lumex::vote_code> lumex::vote_router::vote (std::shared_ptr<lumex::vote> const & vote, lumex::vote_source source, lumex::block_hash filter)
{
	debug_assert (!vote->validate ()); // false => valid vote
	// If present, filter should be set to one of the hashes in the vote
	debug_assert (filter.is_zero () || std::any_of (vote->hashes.begin (), vote->hashes.end (), [&filter] (auto const & hash) {
		return hash == filter;
	}));

	std::unordered_map<lumex::block_hash, lumex::vote_code> results;
	std::unordered_map<lumex::block_hash, std::shared_ptr<lumex::election>> process;
	{
		std::shared_lock lock{ mutex };
		for (auto const & hash : vote->hashes)
		{
			// Ignore votes for other hashes if a filter is set
			if (!filter.is_zero () && hash != filter)
			{
				continue;
			}

			// Ignore duplicate hashes (should not happen with a well-behaved voting node)
			if (results.find (hash) != results.end ())
			{
				continue;
			}

			auto find_election = [this] (auto const & hash) -> std::shared_ptr<lumex::election> {
				if (auto existing = elections.find (hash); existing != elections.end ())
				{
					return existing->second.lock ();
				}
				return {};
			};

			if (auto election = find_election (hash))
			{
				process[hash] = election;
			}
			else
			{
				if (recently_confirmed.contains (hash))
				{
					results[hash] = lumex::vote_code::late;
				}
				else
				{
					results[hash] = lumex::vote_code::indeterminate;
				}
			}
		}
	}

	for (auto const & [block_hash, election] : process)
	{
		auto const vote_result = election->vote (vote->account, vote->timestamp (), block_hash, source);
		results[block_hash] = vote_result;
	}

	// All hashes should have their result set
	debug_assert (!filter.is_zero () || std::all_of (vote->hashes.begin (), vote->hashes.end (), [&results] (auto const & hash) {
		return results.find (hash) != results.end ();
	}));

	// Cache the votes that didn't match any election
	if (source != lumex::vote_source::cache)
	{
		vote_cache.insert (vote, results);
	}

	vote_processed.notify (vote, source, results);

	return results;
}

bool lumex::vote_router::active (lumex::block_hash const & hash) const
{
	std::shared_lock lock{ mutex };
	if (auto existing = elections.find (hash); existing != elections.end ())
	{
		if (auto election = existing->second.lock (); election != nullptr)
		{
			return true;
		}
	}
	return false;
}

std::shared_ptr<lumex::election> lumex::vote_router::election (lumex::block_hash const & hash) const
{
	std::shared_lock lock{ mutex };
	if (auto existing = elections.find (hash); existing != elections.end ())
	{
		if (auto election = existing->second.lock (); election != nullptr)
		{
			return election;
		}
	}
	return nullptr;
}

bool lumex::vote_router::contains (lumex::block_hash const & hash) const
{
	std::shared_lock lock{ mutex };
	return elections.contains (hash);
}

lumex::container_info lumex::vote_router::container_info () const
{
	std::shared_lock lock{ mutex };

	lumex::container_info info;
	info.put ("elections", elections);
	return info;
}

void lumex::vote_router::run ()
{
	std::unique_lock lock{ mutex };
	while (!stopped)
	{
		std::erase_if (elections, [] (auto const & pair) { return pair.second.lock () == nullptr; });
		condition.wait_for (lock, 15s, [&] () { return stopped; });
	}
}

/*
 *
 */

lumex::stat::detail lumex::to_stat_detail (lumex::vote_code code)
{
	return lumex::enum_convert<lumex::stat::detail> (code);
}

std::string_view lumex::to_string (lumex::vote_code code)
{
	return lumex::enum_to_string (code);
}

lumex::stat::detail lumex::to_stat_detail (lumex::vote_source source)
{
	return lumex::enum_convert<lumex::stat::detail> (source);
}

std::string_view lumex::to_string (lumex::vote_source source)
{
	return lumex::enum_to_string (source);
}
