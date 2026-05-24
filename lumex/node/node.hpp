#pragma once

#include <lumex/boost/asio/fwd.hpp>
#include <lumex/lib/fwd.hpp>
#include <lumex/lib/keypair.hpp>
#include <lumex/messages/fwd.hpp>
#include <lumex/node/fwd.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lumex
{
class node final : public std::enable_shared_from_this<node>
{
public:
	node (uint16_t peering_port, std::filesystem::path const & application_path, lumex::work_pool &, lumex::node_flags, unsigned seq = 0);
	node (std::filesystem::path const & application_path, lumex::node_config const &, lumex::work_pool &, lumex::node_flags, unsigned seq = 0);
	~node ();

public:
	void start ();
	void stop ();

	std::shared_ptr<lumex::node> shared ();

	uint64_t store_version () const;
	void inbound (lumex::messages::message const &, std::shared_ptr<lumex::transport::channel> const &);
	void process_active (std::shared_ptr<lumex::block> const &);
	void process_active (std::shared_ptr<lumex::vote> const &);
	std::optional<lumex::block_status> process_local (std::shared_ptr<lumex::block> const &);
	void process_local_async (std::shared_ptr<lumex::block> const &);
	std::shared_ptr<lumex::block> block (lumex::block_hash const &);
	bool block_or_pruned_exists (lumex::block_hash const &) const;
	std::pair<lumex::uint128_t, lumex::uint128_t> balance_pending (lumex::account const &, bool only_confirmed);
	lumex::uint128_t weight (lumex::account const &);
	lumex::uint128_t minimum_principal_weight ();
	void backup_wallet ();
	// The default difficulty updates to base only when the first epoch_2 block is processed
	uint64_t default_difficulty (lumex::work_version const) const;
	uint64_t default_receive_difficulty (lumex::work_version const) const;
	uint64_t max_work_generate_difficulty (lumex::work_version const) const;
	bool local_work_generation_enabled () const;
	bool work_generation_enabled () const;
	bool work_generation_enabled (std::vector<std::pair<std::string, uint16_t>> const &) const;
	std::optional<uint64_t> work_generate_blocking (lumex::block &, uint64_t);
	std::optional<uint64_t> work_generate_blocking (lumex::work_version const, lumex::root const &, uint64_t, std::optional<lumex::account> const & = std::nullopt);
	void work_generate (lumex::work_version const, lumex::root const &, uint64_t, std::function<void (std::optional<uint64_t>)>, std::optional<lumex::account> const & = std::nullopt, bool const = false);
	void add_initial_peers ();
	void start_election (std::shared_ptr<lumex::block> const & block);
	bool warmed_up () const;

	bool block_confirmed (lumex::block_hash const &);
	// This function may spuriously return false after returning true until the database transaction is refreshed
	bool block_confirmed_or_being_confirmed (lumex::secure::transaction const &, lumex::block_hash const &);
	bool block_confirmed_or_being_confirmed (lumex::block_hash const &);

	uint64_t block_count () const;
	uint64_t cemented_count () const;

	bool online () const;

	lumex::bootstrap_weights get_bootstrap_weights () const;

	// Attempts to bootstrap block. This is the best effort, there is no guarantee that the block will be bootstrapped.
	void bootstrap_block (lumex::block_hash const &);

	lumex::messages::telemetry_data local_telemetry () const;

	std::filesystem::path const & get_data_path () const;
	lumex::account get_node_id () const;
	lumex::node_capabilities_flags get_capabilities () const;
	std::string identifier () const;

	lumex::container_info container_info () const;

	// Ledger management
	void copy_with_compaction (std::filesystem::path const & destination);

public:
	const std::filesystem::path application_path; // aka: data_path
	const lumex::keypair node_id;

	std::unique_ptr<lumex::node_config> config_impl;
	lumex::node_config & config;
	std::unique_ptr<lumex::node_flags> flags_impl;
	lumex::node_flags & flags;
	lumex::network_params & network_params;
	std::shared_ptr<boost::asio::io_context> io_ctx_shared;
	boost::asio::io_context & io_ctx;
	std::unique_ptr<lumex::logger> logger_impl;
	lumex::logger & logger;
	std::unique_ptr<lumex::stats> stats_impl;
	lumex::stats & stats;
	std::unique_ptr<lumex::store::ledger_store> store_impl;
	lumex::store::ledger_store & store;
	std::unique_ptr<lumex::wallet::wallets_backend> wallets_backend_impl;
	lumex::wallet::wallets_backend & wallets_backend;
	std::unique_ptr<lumex::ledger> ledger_impl;
	lumex::ledger & ledger;
	std::unique_ptr<lumex::thread_runner> runner_impl;
	lumex::thread_runner & runner;
	std::unique_ptr<lumex::node_observers> observers_impl;
	lumex::node_observers & observers;
	std::unique_ptr<lumex::thread_pool> workers_impl;
	lumex::thread_pool & workers;
	std::unique_ptr<lumex::thread_pool> bootstrap_workers_impl;
	lumex::thread_pool & bootstrap_workers;
	std::unique_ptr<lumex::thread_pool> wallet_workers_impl;
	lumex::thread_pool & wallet_workers;
	std::unique_ptr<lumex::thread_pool> election_workers_impl;
	lumex::thread_pool & election_workers;
	lumex::work_pool & work;
	std::unique_ptr<lumex::distributed_work_factory> distributed_work_impl;
	lumex::distributed_work_factory & distributed_work;
	std::unique_ptr<lumex::unchecked_map> unchecked_impl;
	lumex::unchecked_map & unchecked;
	std::unique_ptr<lumex::ledger_notifications> ledger_notifications_impl;
	lumex::ledger_notifications & ledger_notifications;
	std::unique_ptr<lumex::bandwidth_limiter> outbound_limiter_impl;
	lumex::bandwidth_limiter & outbound_limiter;
	std::unique_ptr<lumex::message_processor> message_processor_impl;
	lumex::message_processor & message_processor;
	std::unique_ptr<lumex::network> network_impl;
	lumex::network & network;
	std::shared_ptr<lumex::transport::channel> loopback_channel;
	std::unique_ptr<lumex::telemetry> telemetry_impl;
	lumex::telemetry & telemetry;
	std::unique_ptr<lumex::transport::tcp_listener> tcp_listener_impl;
	lumex::transport::tcp_listener & tcp_listener;
	std::unique_ptr<lumex::port_mapping> port_mapping_impl;
	lumex::port_mapping & port_mapping;
	std::unique_ptr<lumex::block_processor> block_processor_impl;
	lumex::block_processor & block_processor;
	std::unique_ptr<lumex::fork_cache> fork_cache_impl;
	lumex::fork_cache & fork_cache;
	std::unique_ptr<lumex::cementing_set> cementing_set_impl;
	lumex::cementing_set & cementing_set;
	std::unique_ptr<lumex::bucketing> bucketing_impl;
	lumex::bucketing & bucketing;
	std::unique_ptr<lumex::active_elections> active_impl;
	lumex::active_elections & active;
	std::unique_ptr<lumex::online_reps> online_reps_impl;
	lumex::online_reps & online_reps;
	std::unique_ptr<lumex::wallet::wallets> wallets_impl;
	lumex::wallet::wallets & wallets;
	std::unique_ptr<lumex::rep_crawler> rep_crawler_impl;
	lumex::rep_crawler & rep_crawler;
	std::unique_ptr<lumex::rep_tiers> rep_tiers_impl;
	lumex::rep_tiers & rep_tiers;
	std::unique_ptr<lumex::local_vote_history> history_impl;
	lumex::local_vote_history & history;
	std::unique_ptr<lumex::block_uniquer> block_uniquer_impl;
	lumex::block_uniquer & block_uniquer;
	std::unique_ptr<lumex::vote_uniquer> vote_uniquer_impl;
	lumex::vote_uniquer & vote_uniquer;
	std::unique_ptr<lumex::vote_cache> vote_cache_impl;
	lumex::vote_cache & vote_cache;
	std::unique_ptr<lumex::vote_router> vote_router_impl;
	lumex::vote_router & vote_router;
	std::unique_ptr<lumex::vote_processor> vote_processor_impl;
	lumex::vote_processor & vote_processor;
	std::unique_ptr<lumex::vote_cache_processor> vote_cache_processor_impl;
	lumex::vote_cache_processor & vote_cache_processor;
	std::unique_ptr<lumex::voting_policy> voting_policy_impl;
	lumex::voting_policy & voting_policy;
	std::unique_ptr<lumex::vote_generator> vote_generator_impl;
	lumex::vote_generator & vote_generator;
	std::unique_ptr<lumex::scheduler::component> scheduler_impl;
	lumex::scheduler::component & scheduler;
	std::unique_ptr<lumex::vote_replier> vote_replier_impl;
	lumex::vote_replier & vote_replier;
	std::unique_ptr<lumex::backlog_scan> backlog_scan_impl;
	lumex::backlog_scan & backlog_scan;
	std::unique_ptr<lumex::bounded_backlog> backlog_impl;
	lumex::bounded_backlog & backlog;
	std::unique_ptr<lumex::bootstrap_server> bootstrap_server_impl;
	lumex::bootstrap_server & bootstrap_server;
	std::unique_ptr<lumex::bootstrap_service> bootstrap_impl;
	lumex::bootstrap_service & bootstrap;
	std::unique_ptr<lumex::websocket_server> websocket_impl;
	lumex::websocket_server & websocket;
	std::unique_ptr<lumex::epoch_upgrader> epoch_upgrader_impl;
	lumex::epoch_upgrader & epoch_upgrader;
	std::unique_ptr<lumex::local_block_broadcaster> local_block_broadcaster_impl;
	lumex::local_block_broadcaster & local_block_broadcaster;
	std::unique_ptr<lumex::peer_history> peer_history_impl;
	lumex::peer_history & peer_history;
	std::unique_ptr<lumex::monitor> monitor_impl;
	lumex::monitor & monitor;
	std::unique_ptr<lumex::http_callbacks> http_callbacks_impl;
	lumex::http_callbacks & http_callbacks;
	std::unique_ptr<lumex::pruning> pruning_impl;
	lumex::pruning & pruning;
	std::unique_ptr<lumex::vote_rebroadcaster> vote_rebroadcaster_impl;
	lumex::vote_rebroadcaster & vote_rebroadcaster;
	std::unique_ptr<lumex::block_rebroadcaster> block_rebroadcaster_impl;
	lumex::block_rebroadcaster & block_rebroadcaster;

public:
	std::chrono::steady_clock::time_point const startup_time;
	std::atomic<bool> unresponsive_work_peers{ false };
	std::atomic<bool> stopped{ false };

	// Grace period after startup to allow the node to discover peers and gather online weight
	static constexpr auto warmup_time = std::chrono::minutes{ 5 };

public: // For tests only
	const unsigned node_seq;
	std::optional<uint64_t> work_generate_blocking (lumex::block &);
	std::optional<uint64_t> work_generate_blocking (lumex::root const &, uint64_t);
	std::optional<uint64_t> work_generate_blocking (lumex::root const &);

public: // Testing convenience functions
	[[nodiscard]] lumex::block_status process (std::shared_ptr<lumex::block> block);
	[[nodiscard]] lumex::block_status process (secure::write_transaction const &, std::shared_ptr<lumex::block> block);
	lumex::block_hash latest (lumex::account const &);
	lumex::uint128_t balance (lumex::account const &);

private:
	static std::string make_logger_identifier (lumex::keypair const & node_id);
};

lumex::keypair load_or_create_node_id (std::filesystem::path const & application_path);

lumex::node_flags const & inactive_node_flag_defaults ();
}
