#pragma once

#include <lumex/lib/locks.hpp>
#include <lumex/lib/observer_set.hpp>
#include <lumex/lib/rate_limiting.hpp>
#include <lumex/messages/asc_pull.hpp>
#include <lumex/node/fair_queue_traits.hpp>
#include <lumex/node/fwd.hpp>

#include <atomic>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace lumex
{
class bootstrap_server_config final
{
public:
	lumex::error deserialize (lumex::tomlconfig &);
	lumex::error serialize (lumex::tomlconfig &) const;

public:
	bool enable{ true };
	size_t channel_limit{ 16 };
	size_t threads{ 1 };
	size_t batch_size{ 64 };
	size_t limiter{ 500 };
};

/**
 * Processes bootstrap requests (`asc_pull_req` messages) and replies with bootstrap responses (`asc_pull_ack`)
 */
class bootstrap_server final
{
public:
	bootstrap_server (bootstrap_server_config const &, lumex::store::ledger_store &, lumex::ledger &, lumex::network_constants const &, lumex::stats &);
	~bootstrap_server ();

	void start ();
	void stop ();

	/**
	 * Process `asc_pull_req` message coming from network.
	 * Reply will be sent back over passed in `channel`
	 */
	bool request (lumex::messages::asc_pull_req const & message, std::shared_ptr<lumex::transport::channel> const & channel);

public: // Events
	lumex::observer_set<lumex::messages::asc_pull_ack const &, std::shared_ptr<lumex::transport::channel> const &> on_response;

private:
	// `asc_pull_req` message is small, store by value
	using request_t = std::pair<lumex::messages::asc_pull_req, std::shared_ptr<lumex::transport::channel>>; // <request, response channel>

	void run ();
	void run_batch (lumex::unique_lock<lumex::mutex> & lock);
	lumex::messages::asc_pull_ack process (secure::transaction const &, lumex::messages::asc_pull_req const & message);
	void respond (lumex::messages::asc_pull_ack &, std::shared_ptr<lumex::transport::channel> const &);

	lumex::messages::asc_pull_ack process (secure::transaction const &, lumex::messages::asc_pull_req::id_t id, lumex::messages::empty_payload const & request);

	/*
	 * Blocks request
	 */
	lumex::messages::asc_pull_ack process (secure::transaction const &, lumex::messages::asc_pull_req::id_t id, lumex::messages::asc_pull_req::blocks_payload const & request) const;
	lumex::messages::asc_pull_ack prepare_response (secure::transaction const &, lumex::messages::asc_pull_req::id_t id, lumex::block_hash start_block, std::size_t count) const;
	lumex::messages::asc_pull_ack prepare_empty_blocks_response (lumex::messages::asc_pull_req::id_t id) const;
	std::deque<std::shared_ptr<lumex::block>> prepare_blocks (secure::transaction const &, lumex::block_hash start_block, std::size_t count) const;

	/*
	 * Account info request
	 */
	lumex::messages::asc_pull_ack process (secure::transaction const &, lumex::messages::asc_pull_req::id_t id, lumex::messages::asc_pull_req::account_info_payload const & request) const;

	/*
	 * Frontiers request
	 */
	lumex::messages::asc_pull_ack process (secure::transaction const &, lumex::messages::asc_pull_req::id_t id, lumex::messages::asc_pull_req::frontiers_payload const & request) const;

	/*
	 * Checks if the request should be dropped early on
	 */
	bool verify (lumex::messages::asc_pull_req const & message) const;
	bool verify_request_type (lumex::messages::asc_pull_type) const;

private: // Dependencies
	bootstrap_server_config const & config;
	lumex::store::ledger_store & store;
	lumex::ledger & ledger;
	lumex::network_constants const & network_constants;
	lumex::stats & stats;

private:
	lumex::fair_queue<request_t, lumex::no_value, std::shared_ptr<lumex::transport::channel>> queue;
	lumex::rate_limiter limiter;

	std::atomic<bool> stopped{ false };
	lumex::condition_variable condition;
	mutable lumex::mutex mutex;
	std::vector<std::thread> threads;

public: // Config
	/** Maximum number of blocks to send in a single response, cannot be higher than capacity of a single `asc_pull_ack` message */
	constexpr static std::size_t max_blocks = lumex::messages::asc_pull_ack::blocks_payload::max_blocks;
	constexpr static std::size_t max_frontiers = lumex::messages::asc_pull_ack::frontiers_payload::max_frontiers;
};

lumex::stat::detail to_stat_detail (lumex::messages::asc_pull_type);
}
