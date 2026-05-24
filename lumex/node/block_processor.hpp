#pragma once

#include <lumex/lib/interval.hpp>
#include <lumex/node/block_context.hpp>
#include <lumex/node/block_source.hpp>
#include <lumex/node/fair_queue_traits.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/secure/common.hpp>

#include <boost/thread.hpp>

#include <chrono>
#include <deque>
#include <future>
#include <memory>
#include <optional>
#include <thread>

using namespace std::chrono_literals;

namespace lumex
{
class block_processor_config final
{
public:
	explicit block_processor_config (lumex::network_constants const &);

	lumex::error deserialize (lumex::tomlconfig & toml);
	lumex::error serialize (lumex::tomlconfig & toml) const;

public:
	size_t batch_size{ 256 };

	// Maximum number of blocks to queue from network peers
	size_t max_peer_queue{ 128 };
	// Maximum number of blocks to queue from system components (local RPC, bootstrap)
	size_t max_system_queue{ 16 * 1024 };

	// Higher priority gets processed more frequently
	size_t priority_live{ 1 };
	size_t priority_bootstrap{ 8 };
	size_t priority_local{ 16 };
	size_t priority_system{ 32 };

	bool enable_throttling{ true };
	double backlog_threshold{ 1.5 };
	std::chrono::milliseconds backlog_throttle{ 30ms };
	std::chrono::milliseconds backlog_throttle_max{ 1s };
};

/**
 * Processing blocks is a potentially long IO operation.
 * This class isolates block insertion from other operations like servicing network operations
 */
class block_processor final
{
public:
	block_processor (lumex::node_config const &, lumex::ledger &, lumex::ledger_notifications &, lumex::unchecked_map &, lumex::stats &, lumex::logger &);
	~block_processor ();

	void start ();
	void stop ();

	bool add (
	std::shared_ptr<lumex::block> const & block,
	lumex::block_source source,
	std::shared_ptr<lumex::transport::channel> const & channel = nullptr,
	std::function<void (lumex::block_status)> callback = {});

	std::size_t add_many (
	std::deque<std::shared_ptr<lumex::block>> const & blocks,
	lumex::block_source source,
	std::shared_ptr<lumex::transport::channel> const & channel = nullptr,
	std::function<void (lumex::block_status)> last_callback = {});

	std::optional<lumex::block_status> add_blocking (
	std::shared_ptr<lumex::block> const & block,
	lumex::block_source source);

	void force (std::shared_ptr<lumex::block> const & block);

	std::size_t size () const;
	std::size_t size (lumex::block_source) const;

	lumex::container_info container_info () const;

private: // Dependencies
	block_processor_config const & config;
	lumex::node_config const & node_config;
	lumex::network_params const & network_params;
	lumex::ledger & ledger;
	lumex::ledger_notifications & ledger_notifications;
	lumex::unchecked_map & unchecked;
	lumex::stats & stats;
	lumex::logger & logger;

private:
	void run ();

	// Roll back block in the ledger that conflicts with 'block'
	void rollback_competitor (secure::write_transaction &, lumex::block const & block);
	lumex::block_status process_one (secure::write_transaction const &, lumex::block_context const &, bool forced = false);
	void process_batch (lumex::unique_lock<lumex::mutex> &);
	std::deque<lumex::block_context> next_batch (size_t max_count);
	lumex::block_context next ();
	bool add_impl (lumex::block_context, std::shared_ptr<lumex::transport::channel> const & channel = nullptr);

	void wait_backlog (lumex::unique_lock<lumex::mutex> &);
	double backlog_factor () const;

private:
	lumex::fair_queue<lumex::block_context, lumex::block_source, std::shared_ptr<lumex::transport::channel>> queue;

	bool stopped{ false };
	lumex::condition_variable condition;
	mutable lumex::mutex mutex{ mutex_identifier (mutexes::block_processor) };
	boost::thread thread;

	lumex::interval log_processing_interval;
	lumex::interval log_backlog_interval;
	lumex::interval log_cooldown_interval;
};
}
