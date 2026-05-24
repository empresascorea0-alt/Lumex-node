#include <lumex/lib/blocks.hpp>
#include <lumex/lib/container_info.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/node/recently_cemented_cache.hpp>

#include <ranges>

lumex::recently_cemented_cache::recently_cemented_cache (std::size_t max_size_a) :
	max_size{ max_size_a }
{
}

void lumex::recently_cemented_cache::put (const lumex::election_status & status)
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	entries.emplace_back (entry{ status.winner->qualified_root (), status.winner->hash (), status });
	if (entries.size () > max_size)
	{
		entries.pop_front (); // Remove oldest
	}
}

void lumex::recently_cemented_cache::erase (const lumex::block_hash & hash)
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	entries.get<tag_hash> ().erase (hash);
}

void lumex::recently_cemented_cache::clear ()
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	entries.clear ();
}

auto lumex::recently_cemented_cache::list (size_t max_count) const -> std::deque<lumex::election_status>
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	std::deque<lumex::election_status> result;
	auto it = entries.rbegin ();
	for (size_t i = 0; i < max_count && it != entries.rend (); ++i, ++it)
	{
		result.push_back (it->status);
	}
	return result;
}

std::size_t lumex::recently_cemented_cache::size () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return entries.size ();
}

bool lumex::recently_cemented_cache::contains (const lumex::qualified_root & root) const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return entries.get<tag_root> ().contains (root);
}

bool lumex::recently_cemented_cache::contains (const lumex::block_hash & hash) const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return entries.get<tag_hash> ().contains (hash);
}

lumex::container_info lumex::recently_cemented_cache::container_info () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	lumex::container_info info;
	info.put ("entries", entries);
	return info;
}
