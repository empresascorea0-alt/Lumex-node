#pragma once

#include <lumex/lib/interval.hpp>
#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/threading.hpp>
#include <lumex/node/fair_queue_traits.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/node/transport/channel.hpp>
#include <lumex/node/transport/transport.hpp>

#include <atomic>
#include <condition_variable>
#include <thread>
#include <vector>

namespace lumex
{
class vote_replier_config final
{
public:
	lumex::error deserialize (lumex::tomlconfig &);
	lumex::error serialize (lumex::tomlconfig &) const;

public:
	size_t threads{ std::clamp (lumex::hardware_concurrency () / 2, 1u, 4u) };
	size_t channel_limit{ 128 };
	size_t batch_size{ 16 };
};

/**
 * Handles vote reply requests from the network.
 * Receives confirmation requests, looks up blocks, checks vote eligibility,
 * signs votes and sends replies directly back to the requesting channel.
 */
class vote_replier final
{
public:
	vote_replier (vote_replier_config const &, lumex::voting_policy &, lumex::ledger &, lumex::wallet::wallets &, lumex::network_constants const &, lumex::stats &, lumex::logger &, bool enable_voting);
	~vote_replier ();

	void start ();
	void stop ();

	using request_type = std::vector<std::pair<lumex::block_hash, lumex::root>>;

	bool request (request_type const & request, std::shared_ptr<lumex::transport::channel> const &);

	std::size_t size () const;
	bool empty () const;

	lumex::container_info container_info () const;

private:
	void run ();
	void run_batch (lumex::unique_lock<lumex::mutex> & lock);
	void process (lumex::secure::transaction const &, request_type const &, std::shared_ptr<lumex::transport::channel> const &);

private: // Dependencies
	vote_replier_config const & config;
	lumex::voting_policy & policy;
	lumex::ledger & ledger;
	lumex::wallet::wallets & wallets;
	lumex::network_constants const & network_constants;
	lumex::stats & stats;
	lumex::logger & logger;

private:
	using value_type = std::pair<request_type, std::shared_ptr<lumex::transport::channel>>;
	lumex::fair_queue<value_type, lumex::no_value, std::shared_ptr<lumex::transport::channel>> queue;

	bool const enabled;
	bool stopped{ false };
	lumex::condition_variable condition;
	mutable lumex::mutex mutex{ mutex_identifier (mutexes::vote_replier) };
	std::vector<std::thread> threads;

	lumex::interval log_interval;
};
}
