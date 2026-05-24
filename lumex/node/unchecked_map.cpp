#include <lumex/lib/blocks.hpp>
#include <lumex/lib/locks.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/stats_enums.hpp>
#include <lumex/lib/thread_roles.hpp>
#include <lumex/lib/timer.hpp>
#include <lumex/node/unchecked_map.hpp>

lumex::unchecked_map::unchecked_map (unsigned const max_unchecked_blocks, lumex::stats & stats, bool const & disable_delete) :
	max_unchecked_blocks{ max_unchecked_blocks },
	stats{ stats },
	disable_delete{ disable_delete }
{
}

lumex::unchecked_map::~unchecked_map ()
{
	debug_assert (!thread.joinable ());
}

void lumex::unchecked_map::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread ([this] () {
		lumex::thread_role::set (lumex::thread_role::name::unchecked);
		run ();
	});
}

void lumex::unchecked_map::stop ()
{
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();

	if (thread.joinable ())
	{
		thread.join ();
	}
}

void lumex::unchecked_map::put (lumex::hash_or_account const & dependency, lumex::unchecked_info const & info)
{
	lumex::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	lumex::unchecked_key key{ dependency, info.block->hash () };
	entries.get<tag_root> ().insert ({ key, info });

	if (entries.size () > max_unchecked_blocks)
	{
		entries.get<tag_sequenced> ().pop_front ();
	}

	stats.inc (lumex::stat::type::unchecked, lumex::stat::detail::put);
}

void lumex::unchecked_map::for_each (std::function<void (lumex::unchecked_key const &, lumex::unchecked_info const &)> action, std::function<bool ()> predicate)
{
	lumex::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	for (auto i = entries.begin (), n = entries.end (); predicate () && i != n; ++i)
	{
		action (i->key, i->info);
	}
}

void lumex::unchecked_map::for_each (lumex::hash_or_account const & dependency, std::function<void (lumex::unchecked_key const &, lumex::unchecked_info const &)> action, std::function<bool ()> predicate)
{
	lumex::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	for (auto i = entries.template get<tag_root> ().lower_bound (lumex::unchecked_key{ dependency, 0 }), n = entries.template get<tag_root> ().end (); predicate () && i != n && i->key.key () == dependency.as_block_hash (); ++i)
	{
		action (i->key, i->info);
	}
}

std::vector<lumex::unchecked_info> lumex::unchecked_map::get (lumex::block_hash const & hash)
{
	std::vector<lumex::unchecked_info> result;
	for_each (hash, [&result] (lumex::unchecked_key const & key, lumex::unchecked_info const & info) {
		result.push_back (info);
	});
	return result;
}

bool lumex::unchecked_map::exists (lumex::unchecked_key const & key) const
{
	lumex::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	return entries.get<tag_root> ().count (key) != 0;
}

void lumex::unchecked_map::del (lumex::unchecked_key const & key)
{
	lumex::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	auto erased = entries.get<tag_root> ().erase (key);
	debug_assert (erased);
}

void lumex::unchecked_map::clear ()
{
	lumex::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	entries.clear ();
}

size_t lumex::unchecked_map::entries_size () const
{
	lumex::lock_guard<std::recursive_mutex> lock{ entries_mutex };
	return entries.size ();
}

size_t lumex::unchecked_map::queries_size () const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	return buffer.size ();
}

size_t lumex::unchecked_map::count () const
{
	return entries_size ();
}

void lumex::unchecked_map::trigger (lumex::hash_or_account const & dependency)
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	buffer.emplace_back (dependency);
	lock.unlock ();
	stats.inc (lumex::stat::type::unchecked, lumex::stat::detail::trigger);
	condition.notify_all (); // Notify run ()
}

void lumex::unchecked_map::process_queries (decltype (buffer) const & back_buffer)
{
	for (auto const & item : back_buffer)
	{
		query_impl (item.hash);
	}
}

void lumex::unchecked_map::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		if (!buffer.empty ())
		{
			back_buffer.swap (buffer);
			writing_back_buffer = true;
			lock.unlock ();
			process_queries (back_buffer);
			lock.lock ();
			writing_back_buffer = false;
			back_buffer.clear ();
		}
		else
		{
			condition.wait (lock, [this] () {
				return stopped || !buffer.empty ();
			});
		}
	}
}

void lumex::unchecked_map::query_impl (lumex::block_hash const & hash)
{
	std::deque<lumex::unchecked_key> delete_queue;
	for_each (hash, [this, &delete_queue] (lumex::unchecked_key const & key, lumex::unchecked_info const & info) {
		delete_queue.push_back (key);
		stats.inc (lumex::stat::type::unchecked, lumex::stat::detail::satisfied);
		satisfied.notify (info);
	});
	if (!disable_delete)
	{
		for (auto const & key : delete_queue)
		{
			del (key);
		}
	}
}

lumex::container_info lumex::unchecked_map::container_info () const
{
	lumex::container_info info;
	info.put ("entries", entries_size ());
	info.put ("queries", queries_size ());
	return info;
}
