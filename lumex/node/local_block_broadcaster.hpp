#pragma once

#include <lumex/lib/blocks.hpp>
#include <lumex/lib/constants.hpp>
#include <lumex/lib/interval.hpp>
#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/lib/processing_queue.hpp>
#include <lumex/lib/rate_limiting.hpp>
#include <lumex/node/block_processor.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <memory>
#include <thread>
#include <unordered_set>

using namespace std::chrono_literals;
namespace mi = boost::multi_index;

namespace lumex
{
class local_block_broadcaster_config final
{
public:
	explicit local_block_broadcaster_config (lumex::network_constants const & network)
	{
		if (network.is_dev_network ())
		{
			rebroadcast_interval = 1s;
			cleanup_interval = 1s;
		}
	}

	lumex::error deserialize (lumex::tomlconfig &);
	lumex::error serialize (lumex::tomlconfig &) const;

public:
	bool enable{ true };
	std::size_t max_size{ 1024 * 8 };
	std::chrono::seconds rebroadcast_interval{ 3 };
	std::chrono::seconds max_rebroadcast_interval{ 60 };
	std::size_t broadcast_rate_limit{ 32 };
	double broadcast_rate_burst_ratio{ 3 };
	std::chrono::seconds cleanup_interval{ 60 };
};

/**
 * Broadcasts blocks to the network
 * Tracks local blocks for more aggressive propagation
 */
class local_block_broadcaster final
{
public:
	local_block_broadcaster (local_block_broadcaster_config const &, lumex::node &, lumex::ledger_notifications &, lumex::network &, lumex::cementing_set &, lumex::stats &, lumex::logger &);
	~local_block_broadcaster ();

	void start ();
	void stop ();

	bool contains (lumex::block_hash const &) const;
	size_t size () const;

	lumex::container_info container_info () const;

private:
	void run ();
	void run_broadcasts (lumex::unique_lock<lumex::mutex> &);
	void cleanup (lumex::unique_lock<lumex::mutex> &);
	std::chrono::milliseconds rebroadcast_interval (unsigned rebroadcasts) const;

private: // Dependencies
	local_block_broadcaster_config const & config;
	lumex::node & node;
	lumex::ledger_notifications & ledger_notifications;
	lumex::network & network;
	lumex::cementing_set & cementing_set;
	lumex::stats & stats;
	lumex::logger & logger;

private:
	struct local_entry
	{
		std::shared_ptr<lumex::block> block;
		std::chrono::steady_clock::time_point arrival;

		std::chrono::steady_clock::time_point last_broadcast{};
		std::chrono::steady_clock::time_point next_broadcast{};
		unsigned rebroadcasts{ 0 };

		lumex::block_hash hash () const
		{
			return block->hash ();
		}
	};

	// clang-format off
	class tag_sequenced	{};
	class tag_hash {};
	class tag_broadcast {};

	using ordered_locals = boost::multi_index_container<local_entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::const_mem_fun<local_entry, lumex::block_hash, &local_entry::hash>>,
		mi::ordered_non_unique<mi::tag<tag_broadcast>,
			mi::member<local_entry, std::chrono::steady_clock::time_point, &local_entry::next_broadcast>>
	>>;
	// clang-format on

	ordered_locals local_blocks;

private:
	lumex::rate_limiter limiter;
	lumex::interval cleanup_interval;

	std::atomic<bool> stopped{ false };
	lumex::condition_variable condition;
	mutable lumex::mutex mutex;
	std::thread thread;

	lumex::interval log_interval;
};
}
