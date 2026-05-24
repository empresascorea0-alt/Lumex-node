#pragma once

#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/lib/observer_set.hpp>
#include <lumex/lib/rate_limiting.hpp>
#include <lumex/lib/thread_pool.hpp>
#include <lumex/node/bucketing.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/thread.hpp>

#include <unordered_map>

namespace mi = boost::multi_index;

namespace lumex
{
class backlog_index
{
public:
	struct priority_key
	{
		lumex::bucket_index bucket;
		lumex::priority_timestamp priority;

		auto operator<=> (priority_key const &) const = default;
	};

	struct entry
	{
		lumex::block_hash hash;
		lumex::account account;
		lumex::bucket_index bucket;
		lumex::priority_timestamp priority;

		backlog_index::priority_key priority_key () const
		{
			return { bucket, priority };
		}
	};

public:
	backlog_index () = default;

	bool insert (lumex::block const & block, lumex::bucket_index, lumex::priority_timestamp);

	bool erase (lumex::account const & account);
	bool erase (lumex::block_hash const & hash);

	using filter_callback = std::function<bool (lumex::block_hash const &)>;
	std::deque<lumex::block_hash> top (lumex::bucket_index, size_t count, filter_callback const &) const;

	std::deque<lumex::block_hash> next (lumex::block_hash last, size_t count) const;

	bool contains (lumex::block_hash const & hash) const;
	size_t size () const;
	size_t size (lumex::bucket_index) const;

	lumex::container_info container_info () const;

private:
	// clang-format off
	class tag_hash {};
	class tag_hash_ordered {};
	class tag_account {};
	class tag_priority {};

	using ordered_blocks = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_hash>, // Allows for fast lookup
			mi::member<entry, lumex::block_hash, &entry::hash>>,
		mi::ordered_unique<mi::tag<tag_hash_ordered>, // Allows for sequential scan
			mi::member<entry, lumex::block_hash, &entry::hash>>,
		mi::hashed_non_unique<mi::tag<tag_account>,
			mi::member<entry, lumex::account, &entry::account>>,
		mi::ordered_non_unique<mi::tag<tag_priority>,
			mi::const_mem_fun<entry, priority_key, &entry::priority_key>, std::greater<>> // DESC order
	>>;
	// clang-format on

	ordered_blocks blocks;

	// Keep track of the size of the backlog in number of unconfirmed blocks per bucket
	std::unordered_map<lumex::bucket_index, size_t> size_by_bucket;
};

class bounded_backlog_config
{
public:
	lumex::error deserialize (lumex::tomlconfig &);
	lumex::error serialize (lumex::tomlconfig &) const;

public:
	bool enable{ true };
	size_t batch_size{ 32 };
	size_t scan_rate{ 64 };
};

class bounded_backlog
{
public:
	bounded_backlog (lumex::node_config const &, lumex::node &, lumex::ledger &, lumex::ledger_notifications &, lumex::bucketing &, lumex::backlog_scan &, lumex::block_processor &, lumex::cementing_set &, lumex::stats &, lumex::logger &);
	~bounded_backlog ();

	void start ();
	void stop ();

	size_t index_size () const;
	bool contains (lumex::block_hash const &) const;

	lumex::container_info container_info () const;

private: // Dependencies
	lumex::node_config const & config;
	lumex::node & node;
	lumex::ledger & ledger;
	lumex::ledger_notifications & ledger_notifications;
	lumex::bucketing & bucketing;
	lumex::backlog_scan & backlog_scan;
	lumex::cementing_set & cementing_set;
	lumex::stats & stats;
	lumex::logger & logger;

private:
	void activate (lumex::secure::transaction &, lumex::account const &, lumex::account_info const &, lumex::confirmation_height_info const &);
	void update (lumex::secure::transaction const &, lumex::block_hash const &);
	bool insert (lumex::secure::transaction const &, lumex::block const &);

	bool predicate () const;
	void run ();
	std::deque<lumex::block_hash> gather_targets (size_t max_count, size_t bucket_threshold) const;
	bool should_rollback (lumex::block_hash const &) const;

	std::deque<lumex::block_hash> perform_rollbacks (std::deque<lumex::block_hash> const & targets, size_t max_rollbacks);

	void run_scan ();

private:
	lumex::backlog_index index;

	lumex::rate_limiter scan_limiter;

	std::atomic<bool> stopped{ false };
	lumex::condition_variable condition;
	mutable lumex::mutex mutex;
	boost::thread thread;
	std::thread scan_thread;
};
}