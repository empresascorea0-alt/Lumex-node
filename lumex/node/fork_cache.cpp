#include <lumex/lib/blocks.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/tomlconfig.hpp>
#include <lumex/node/fork_cache.hpp>

#include <boost/range/iterator_range.hpp>

lumex::fork_cache::fork_cache (lumex::fork_cache_config const & config_a, lumex::stats & stats_a) :
	config{ config_a },
	stats{ stats_a }
{
}

void lumex::fork_cache::put (std::shared_ptr<lumex::block> block)
{
	release_assert (block != nullptr);

	std::lock_guard guard{ mutex };

	// Add the new block to the cache, duplicates are prevented by the multi_index container
	auto [it, added] = roots.push_back ({ block->qualified_root () });
	release_assert (it != roots.end ());
	stats.inc (lumex::stat::type::fork_cache, added ? lumex::stat::detail::insert : lumex::stat::detail::duplicate);

	// Check if we already have this hash
	bool exists = std::find_if (it->forks.begin (), it->forks.end (), [&block] (auto const & fork) {
		return fork->hash () == block->hash ();
	})
	!= it->forks.end ();

	if (exists)
	{
		return;
	}

	it->forks.push_back (block);

	// Check if we have too many forks for this root
	if (it->forks.size () > config.max_forks_per_root)
	{
		stats.inc (lumex::stat::type::fork_cache, lumex::stat::detail::overfill_entry);
		it->forks.pop_front (); // Remove the oldest entry
	}

	// Check if we're at capacity
	if (roots.size () > config.max_size)
	{
		// Remove oldest entry (first in sequence)
		stats.inc (lumex::stat::type::fork_cache, lumex::stat::detail::overfill);
		roots.pop_front (); // Remove the oldest entry
	}
}

std::deque<std::shared_ptr<lumex::block>> lumex::fork_cache::get (lumex::qualified_root const & root) const
{
	std::lock_guard guard{ mutex };

	if (auto it = roots.get<tag_root> ().find (root); it != roots.get<tag_root> ().end ())
	{
		return it->forks;
	}
	return {};
}

size_t lumex::fork_cache::size () const
{
	std::lock_guard guard{ mutex };

	return roots.size ();
}

bool lumex::fork_cache::contains (lumex::qualified_root const & root) const
{
	std::lock_guard guard{ mutex };

	return roots.get<tag_root> ().count (root) > 0;
}

lumex::container_info lumex::fork_cache::container_info () const
{
	std::lock_guard guard{ mutex };

	lumex::container_info result;
	result.put ("roots", roots);
	return result;
}

/*
 * fork_cache_config
 */

lumex::error lumex::fork_cache_config::deserialize (lumex::tomlconfig & toml)
{
	toml.get ("max_size", max_size);
	toml.get ("max_forks_per_root", max_forks_per_root);

	return toml.get_error ();
}

lumex::error lumex::fork_cache_config::serialize (lumex::tomlconfig & toml) const
{
	toml.put ("max_size", max_size, "Maximum number of roots in the cache. Each root can have multiple forks. \ntype:uint64");
	toml.put ("max_forks_per_root", max_forks_per_root, "Maximum number of forks per root. \ntype:uint64");

	return toml.get_error ();
}