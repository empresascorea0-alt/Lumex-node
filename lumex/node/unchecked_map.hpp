#pragma once

#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/observer_set.hpp>
#include <lumex/secure/common.hpp>

#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <thread>

namespace mi = boost::multi_index;

namespace lumex
{
class stats;

class unchecked_map
{
public:
	unchecked_map (unsigned const max_unchecked_blocks, lumex::stats &, bool const & do_delete);
	~unchecked_map ();

	void start ();
	void stop ();

	void put (lumex::hash_or_account const & dependency, lumex::unchecked_info const & info);
	void for_each (
	std::function<void (lumex::unchecked_key const &, lumex::unchecked_info const &)> action, std::function<bool ()> predicate = [] () { return true; });
	void for_each (
	lumex::hash_or_account const & dependency, std::function<void (lumex::unchecked_key const &, lumex::unchecked_info const &)> action, std::function<bool ()> predicate = [] () { return true; });
	std::vector<lumex::unchecked_info> get (lumex::block_hash const &);
	bool exists (lumex::unchecked_key const & key) const;
	void del (lumex::unchecked_key const & key);
	void clear ();

	/**
	 * Trigger requested dependencies
	 */
	void trigger (lumex::hash_or_account const & dependency);

	size_t count () const; // Same as `entries_size ()`
	size_t entries_size () const;
	size_t queries_size () const;

	lumex::container_info container_info () const;

public: // Events
	lumex::observer_set<lumex::unchecked_info const &> satisfied;

private:
	void run ();
	void query_impl (lumex::block_hash const & hash);

private: // Dependencies
	lumex::stats & stats;

private:
	bool const & disable_delete;
	std::deque<lumex::hash_or_account> buffer;
	std::deque<lumex::hash_or_account> back_buffer;
	bool writing_back_buffer{ false };

	bool stopped{ false };
	lumex::condition_variable condition;
	mutable lumex::mutex mutex; // Protects queries
	std::thread thread;

	unsigned const max_unchecked_blocks;

	void process_queries (decltype (buffer) const & back_buffer);

private:
	struct entry
	{
		lumex::unchecked_key key;
		lumex::unchecked_info info;
	};

	// clang-format off
	class tag_sequenced {};
	class tag_root {};

	using ordered_unchecked = boost::multi_index_container<entry,
		mi::indexed_by<
			mi::sequenced<mi::tag<tag_sequenced>>,
			mi::ordered_unique<mi::tag<tag_root>,
				mi::member<entry, lumex::unchecked_key, &entry::key>>>>;
	// clang-format on
	ordered_unchecked entries;

	mutable std::recursive_mutex entries_mutex; // Protects entries
};
}
