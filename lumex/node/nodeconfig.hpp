#pragma once

#include <lumex/lib/config.hpp>
#include <lumex/lib/errors.hpp>
#include <lumex/lib/indirect.hpp>
#include <lumex/lib/node_capabilities.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/ratios.hpp>
#include <lumex/lib/threading.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/generate_cache_flags.hpp>
#include <lumex/secure/network_params.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <vector>

namespace lumex
{
class tomlconfig;

class node_config
{
public:
	node_config (lumex::network_params const & = lumex::dev::network_params);
	node_config (node_config const &);
	node_config (node_config &&) noexcept;
	~node_config ();

	lumex::error serialize_toml (lumex::tomlconfig &) const;
	lumex::error deserialize_toml (lumex::tomlconfig &);

	lumex::account random_representative () const;

public:
	lumex::network_params network_params;

	std::optional<uint16_t> peering_port{};
	std::vector<std::pair<std::string, uint16_t>> work_peers;
	std::vector<std::pair<std::string, uint16_t>> secondary_work_peers{ { "127.0.0.1", 8076 } }; // Default of lumex-pow-server
	std::vector<std::string> preconfigured_peers;
	std::vector<lumex::account> preconfigured_representatives;
	unsigned bootstrap_fraction_numerator{ 1 };
	lumex::amount receive_minimum{ lumex::lumex_ratio / 1000 / 1000 }; // 0.000001 lumex
	lumex::amount vote_minimum{ lumex::Klumex_ratio }; // 1000 lumex
	lumex::amount rep_crawler_weight_minimum{ "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF" };
	lumex::amount online_weight_minimum{ 60000 * lumex::Klumex_ratio }; // 60 million lumex
	/**
	 * The minimum vote weight that a representative must have for its vote to be counted.
	 * All representatives above this weight will be kept in memory! */
	lumex::amount representative_vote_weight_minimum{ 10 * lumex::lumex_ratio };
	unsigned password_fanout{ 1024 };
	unsigned io_threads{ env_io_threads ().value_or (std::max (4u, lumex::hardware_concurrency ())) };
	unsigned network_threads{ std::max (4u, lumex::hardware_concurrency ()) };
	unsigned work_threads{ std::max (4u, lumex::hardware_concurrency ()) };
	unsigned background_threads{ std::max (4u, lumex::hardware_concurrency ()) };
	unsigned wallet_threads{ 4 };
	unsigned signature_checker_threads{ std::max (2u, lumex::hardware_concurrency () / 2) };
	bool enable_voting{ false };
	unsigned bootstrap_connections{ 4 };
	unsigned bootstrap_connections_max{ 64 };
	unsigned bootstrap_initiator_threads{ 1 };
	unsigned bootstrap_serving_threads{ 1 };
	uint32_t bootstrap_frontier_request_count{ 1024 * 1024 };
	std::string callback_address;
	uint16_t callback_port{ 0 };
	std::string callback_target;
	bool allow_local_peers{ !(network_params.network.is_live_network () || network_params.network.is_test_network ()) };
	std::string external_address;
	uint16_t external_port{ 0 };
	std::chrono::milliseconds block_processor_batch_max_time{ std::chrono::milliseconds (500) };
	std::chrono::seconds unchecked_cutoff_time{ std::chrono::seconds (4 * 60 * 60) };
	std::chrono::lumexseconds pow_sleep_interval{ 0 };
	bool use_memory_pools{ true };
	static std::chrono::minutes constexpr wallet_backup_interval = std::chrono::minutes (5);
	/** Default outbound traffic shaping is 10MB/s */
	std::size_t bandwidth_limit{ 10 * 1024 * 1024 };
	double bandwidth_limit_burst_ratio{ 3. };
	/** Default bootstrap outbound traffic limit is 5MB/s */
	std::size_t bootstrap_bandwidth_limit{ 5 * 1024 * 1024 };
	double bootstrap_bandwidth_burst_ratio{ 1. };
	bool backup_before_upgrade{ false };
	double max_work_generate_multiplier{ 64. };
	uint32_t max_queued_requests{ 512 };
	unsigned max_unchecked_blocks{ 65536 };
	std::size_t max_backlog{ 100000 };
	std::chrono::seconds max_pruning_age{ !network_params.network.is_beta_network () ? std::chrono::seconds (24 * 60 * 60) : std::chrono::seconds (5 * 60) }; // 1 day; 5 minutes for beta network
	uint64_t max_pruning_depth{ 0 };
	lumex::database_backend database_backend{ lumex::default_database_backend () };
	bool enable_upnp{ true };
	std::size_t max_ledger_notifications{ 300 };

public: // Subsystem configs
	lumex::indirect<lumex::scheduler::optimistic_config> optimistic_scheduler;
	lumex::indirect<lumex::scheduler::hinted_config> hinted_scheduler;
	lumex::indirect<lumex::scheduler::priority_config> priority_scheduler;
	lumex::indirect<lumex::websocket::config> websocket_config;
	lumex::indirect<lumex::store::txn_tracking_config> txn_tracking;
	lumex::indirect<lumex::stats_config> stats_config;
	lumex::indirect<lumex::ipc::ipc_config> ipc_config;
	lumex::indirect<lumex::bootstrap_config> bootstrap;
	lumex::indirect<lumex::bootstrap_server_config> bootstrap_server;
	lumex::indirect<lumex::rocksdb_config> rocksdb_config;
	lumex::indirect<lumex::lmdb_config> lmdb_config;
	lumex::indirect<lumex::vote_cache_config> vote_cache;
	lumex::indirect<lumex::rep_crawler_config> rep_crawler;
	lumex::indirect<lumex::block_processor_config> block_processor;
	lumex::indirect<lumex::active_elections_config> active_elections;
	lumex::indirect<lumex::vote_generator_config> vote_generator;
	lumex::indirect<lumex::vote_processor_config> vote_processor;
	lumex::indirect<lumex::peer_history_config> peer_history;
	lumex::indirect<lumex::transport::tcp_config> tcp;
	lumex::indirect<lumex::vote_replier_config> vote_replier;
	lumex::indirect<lumex::message_processor_config> message_processor;
	lumex::indirect<lumex::network_config> network;
	lumex::indirect<lumex::local_block_broadcaster_config> local_block_broadcaster;
	lumex::indirect<lumex::cementing_set_config> cementing_set;
	lumex::indirect<lumex::monitor_config> monitor;
	lumex::indirect<lumex::backlog_scan_config> backlog_scan;
	lumex::indirect<lumex::bounded_backlog_config> bounded_backlog;
	lumex::indirect<lumex::vote_rebroadcaster_config> vote_rebroadcaster;
	lumex::indirect<lumex::block_rebroadcaster_config> block_rebroadcaster;
	lumex::indirect<lumex::fork_cache_config> fork_cache;

public:
	/** Entry is ignored if it cannot be parsed as a valid address:port */
	static void deserialize_address (std::string const &, std::vector<std::pair<std::string, uint16_t>> &);
	static std::optional<unsigned> env_io_threads ();
};

class node_flags final
{
public:
	std::vector<std::string> config_overrides;
	std::vector<std::string> rpc_config_overrides;
	bool disable_add_initial_peers{ false };
	bool disable_activate_successors{ false };
	bool disable_backup{ false };
	bool disable_lazy_bootstrap{ false };
	bool disable_legacy_bootstrap{ false };
	bool disable_wallet_bootstrap{ false };
	bool disable_bootstrap_listener{ false };
	bool disable_bootstrap_bulk_pull_server{ false };
	bool disable_bootstrap_bulk_push_client{ false };
	bool disable_ongoing_bootstrap{ false };
	bool disable_reachout{ false };
	bool disable_reachout_preconfigured{ false };
	bool disable_rep_crawler{ false };
	bool disable_request_loop{ false };
	bool disable_tcp_realtime{ false };
	bool disable_providing_telemetry_metrics{ false };
	bool disable_block_processor_unchecked_deletion{ false };
	bool allow_bootstrap_peers_duplicates{ false };
	bool disable_max_peers_per_ip{ false };
	bool disable_max_peers_per_subnetwork{ false };
	bool disable_search_pending{ false };
	bool enable_pruning{ false };
	bool disable_topo_index{ false };
	bool enable_rpc{ false };
	bool enable_voting{ false };
	bool super_rebroadcaster{ false };
	bool fast_bootstrap{ false };
	std::optional<std::filesystem::path> runtime_info_file; // Write runtime_info.json with ports
	bool read_only{ false };
	bool disable_connection_cleanup{ false };
	lumex::generate_cache_flags generate_cache;
	bool inactive_node{ false };
	std::size_t block_processor_batch_size{ 0 };
	std::size_t block_processor_full_size{ 65536 };
	std::size_t block_processor_verification_size{ 0 };
	std::size_t vote_processor_capacity{ 144 * 1024 };
	std::size_t bootstrap_interval{ 0 };
	std::optional<lumex::node_capabilities_flags> capabilities_override;
};
}
