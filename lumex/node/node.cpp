#include <lumex/lib/block_type.hpp>
#include <lumex/lib/block_uniquer.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/files.hpp>
#include <lumex/lib/formatting.hpp>
#include <lumex/lib/keypair.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/node_capabilities.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/stream.hpp>
#include <lumex/lib/thread_pool.hpp>
#include <lumex/lib/thread_runner.hpp>
#include <lumex/lib/tomlconfig.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/lib/version.hpp>
#include <lumex/lib/vote.hpp>
#include <lumex/lib/work.hpp>
#include <lumex/lib/work_version.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/backlog_scan.hpp>
#include <lumex/node/bandwidth_limiter.hpp>
#include <lumex/node/block_rebroadcaster.hpp>
#include <lumex/node/bootstrap/bootstrap_server.hpp>
#include <lumex/node/bootstrap/bootstrap_service.hpp>
#include <lumex/node/bounded_backlog.hpp>
#include <lumex/node/bucketing.hpp>
#include <lumex/node/cementing_set.hpp>
#include <lumex/node/daemonconfig.hpp>
#include <lumex/node/distributed_work_factory.hpp>
#include <lumex/node/election.hpp>
#include <lumex/node/election_status.hpp>
#include <lumex/node/endpoint.hpp>
#include <lumex/node/epoch_upgrader.hpp>
#include <lumex/node/fork_cache.hpp>
#include <lumex/node/ledger_notifications.hpp>
#include <lumex/node/local_block_broadcaster.hpp>
#include <lumex/node/local_vote_history.hpp>
#include <lumex/node/make_store.hpp>
#include <lumex/node/message_processor.hpp>
#include <lumex/node/monitor.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/node_observers.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/online_reps.hpp>
#include <lumex/node/peer_history.hpp>
#include <lumex/node/portmapping.hpp>
#include <lumex/node/pruning.hpp>
#include <lumex/node/rep_tiers.hpp>
#include <lumex/node/repcrawler.hpp>
#include <lumex/node/rpc_callbacks.hpp>
#include <lumex/node/scheduler/component.hpp>
#include <lumex/node/scheduler/hinted.hpp>
#include <lumex/node/scheduler/manual.hpp>
#include <lumex/node/scheduler/optimistic.hpp>
#include <lumex/node/scheduler/priority.hpp>
#include <lumex/node/telemetry.hpp>
#include <lumex/node/transport/loopback.hpp>
#include <lumex/node/transport/tcp_listener.hpp>
#include <lumex/node/unchecked_map.hpp>
#include <lumex/node/vote_cache.hpp>
#include <lumex/node/vote_generator.hpp>
#include <lumex/node/vote_processor.hpp>
#include <lumex/node/vote_rebroadcaster.hpp>
#include <lumex/node/vote_replier.hpp>
#include <lumex/node/vote_router.hpp>
#include <lumex/node/wallet.hpp>
#include <lumex/node/websocket.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_set_any.hpp>
#include <lumex/secure/ledger_set_cemented.hpp>
#include <lumex/secure/voting_policy.hpp>
#include <lumex/store/ledger/account.hpp>
#include <lumex/store/ledger/block.hpp>
#include <lumex/store/ledger/confirmation_height.hpp>
#include <lumex/store/ledger/final_vote.hpp>
#include <lumex/store/ledger/online_weight.hpp>
#include <lumex/store/ledger/peer.hpp>
#include <lumex/store/ledger/pending.hpp>
#include <lumex/store/ledger/pruned.hpp>
#include <lumex/store/ledger/rep_weight.hpp>
#include <lumex/store/ledger/version.hpp>
#include <lumex/store/ledger_store.hpp>
#include <lumex/store/rocksdb/backend_rocksdb.hpp>
#include <lumex/wallet/lmdb/wallets_backend_lmdb.hpp>
#include <lumex/weights/bootstrap_weights.hpp>

#include <boost/format.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <future>
#include <sstream>

lumex::node::node (uint16_t peering_port_a, std::filesystem::path const & application_path_a, lumex::work_pool & work_a, lumex::node_flags flags_a, unsigned seq) :
	node (
	application_path_a, [&] { lumex::node_config c; c.peering_port = peering_port_a; return c; }(), work_a, flags_a, seq)
{
}

lumex::node::node (std::filesystem::path const & application_path_a, lumex::node_config const & config_a, lumex::work_pool & work_a, lumex::node_flags flags_a, unsigned seq) :
	application_path{ application_path_a },
	node_id{ load_or_create_node_id (application_path_a) },
	config_impl{ std::make_unique<lumex::node_config> (config_a) },
	config{ *config_impl },
	flags_impl{ std::make_unique<lumex::node_flags> (flags_a) },
	flags{ *flags_impl },
	network_params{ config.network_params },
	io_ctx_shared{ std::make_shared<boost::asio::io_context> () },
	io_ctx{ *io_ctx_shared },
	logger_impl{ std::make_unique<lumex::logger> (make_logger_identifier (node_id)) },
	logger{ *logger_impl },
	stats_impl{ std::make_unique<lumex::stats> (logger, config.stats_config) },
	stats{ *stats_impl },
	store_impl{ lumex::make_store (logger, stats, application_path_a, network_params.ledger, flags.read_only, true, config_a) },
	store{ *store_impl },
	wallets_backend_impl{ std::make_unique<lumex::wallet::lmdb::wallets_backend_lmdb> (application_path_a / "wallets.ldb", config_a.lmdb_config) },
	wallets_backend{ *wallets_backend_impl },
	ledger_impl{ std::make_unique<lumex::ledger> (store, network_params, stats, logger,
	lumex::ledger_options{
	.generate_cache = flags_a.generate_cache,
	.min_rep_weight = config.representative_vote_weight_minimum.number (),
	.max_backlog = config.max_backlog,
	// Topo index is incompatible with pruning, so disable it on fresh ledgers when pruning is on
	// For existing ledgers the persisted meta flag wins, so this only affects first-init
	.enable_topo_index = !(flags_a.enable_pruning || flags_a.disable_topo_index) }) },
	ledger{ *ledger_impl },
	runner_impl{ std::make_unique<lumex::thread_runner> (io_ctx_shared, logger, config.io_threads) },
	runner{ *runner_impl },
	observers_impl{ std::make_unique<lumex::node_observers> () },
	observers{ *observers_impl },
	workers_impl{ std::make_unique<lumex::thread_pool> (config.background_threads, lumex::thread_role::name::worker, /* start immediately */ true) },
	workers{ *workers_impl },
	bootstrap_workers_impl{ std::make_unique<lumex::thread_pool> (config.bootstrap_serving_threads, lumex::thread_role::name::bootstrap_worker, /* start immediately */ true) },
	bootstrap_workers{ *bootstrap_workers_impl },
	wallet_workers_impl{ std::make_unique<lumex::thread_pool> (1, lumex::thread_role::name::wallet_worker, /* start immediately */ true) },
	wallet_workers{ *wallet_workers_impl },
	election_workers_impl{ std::make_unique<lumex::thread_pool> (1, lumex::thread_role::name::election_worker, /* start immediately */ true) },
	election_workers{ *election_workers_impl },
	work{ work_a },
	distributed_work_impl{ std::make_unique<lumex::distributed_work_factory> (*this) },
	distributed_work{ *distributed_work_impl },
	unchecked_impl{ std::make_unique<lumex::unchecked_map> (config.max_unchecked_blocks, stats, flags.disable_block_processor_unchecked_deletion) },
	unchecked{ *unchecked_impl },
	ledger_notifications_impl{ std::make_unique<lumex::ledger_notifications> (config, stats, logger) },
	ledger_notifications{ *ledger_notifications_impl },
	outbound_limiter_impl{ std::make_unique<lumex::bandwidth_limiter> (config, flags) },
	outbound_limiter{ *outbound_limiter_impl },
	message_processor_impl{ std::make_unique<lumex::message_processor> (config.message_processor, *this) },
	message_processor{ *message_processor_impl },
	// empty `config.peering_port` means the user made no port choice at all;
	// otherwise, any value is considered, with `0` having the special meaning of 'let the OS pick a port instead'
	//
	network_impl{ std::make_unique<lumex::network> (*this, config.peering_port.has_value () ? *config.peering_port : 0) },
	network{ *network_impl },
	loopback_channel{ std::make_shared<lumex::transport::loopback_channel> (*this) },
	telemetry_impl{ std::make_unique<lumex::telemetry> (flags, *this, network, observers, network_params, stats) },
	telemetry{ *telemetry_impl },
	// BEWARE: `bootstrap` takes `network.port` instead of `config.peering_port` because when the user doesn't specify
	//         a peering port and wants the OS to pick one, the picking happens when `network` gets initialized
	//         (if UDP is active, otherwise it happens when `bootstrap` gets initialized), so then for TCP traffic
	//         we want to tell `bootstrap` to use the already picked port instead of itself picking a different one.
	//         Thus, be very careful if you change the order: if `bootstrap` gets constructed before `network`,
	//         the latter would inherit the port from the former (if TCP is active, otherwise `network` picks first)
	//
	tcp_listener_impl{ std::make_unique<lumex::transport::tcp_listener> (network.port, config.tcp, *this) },
	tcp_listener{ *tcp_listener_impl },
	port_mapping_impl{ std::make_unique<lumex::port_mapping> (*this) },
	port_mapping{ *port_mapping_impl },
	block_processor_impl{ std::make_unique<lumex::block_processor> (config, ledger, ledger_notifications, unchecked, stats, logger) },
	block_processor{ *block_processor_impl },
	fork_cache_impl{ std::make_unique<lumex::fork_cache> (config.fork_cache, stats) },
	fork_cache{ *fork_cache_impl },
	cementing_set_impl{ std::make_unique<lumex::cementing_set> (config.cementing_set, ledger, ledger_notifications, stats, logger) },
	cementing_set{ *cementing_set_impl },
	bucketing_impl{ std::make_unique<lumex::bucketing> () },
	bucketing{ *bucketing_impl },
	active_impl{ std::make_unique<lumex::active_elections> (*this, ledger_notifications, cementing_set) },
	active{ *active_impl },
	online_reps_impl{ std::make_unique<lumex::online_reps> (config, *this, ledger, stats, logger) },
	online_reps{ *online_reps_impl },
	wallets_impl{ std::make_unique<lumex::wallet::wallets> (*this, wallets_backend, ledger, config, network_params, online_reps, network, stats, logger) },
	wallets{ *wallets_impl },
	rep_crawler_impl{ std::make_unique<lumex::rep_crawler> (config.rep_crawler, *this) },
	rep_crawler{ *rep_crawler_impl },
	rep_tiers_impl{ std::make_unique<lumex::rep_tiers> (ledger, network_params, online_reps, stats, logger) },
	rep_tiers{ *rep_tiers_impl },
	history_impl{ std::make_unique<lumex::local_vote_history> (config.network_params.voting) },
	history{ *history_impl },
	block_uniquer_impl{ std::make_unique<lumex::block_uniquer> () },
	block_uniquer{ *block_uniquer_impl },
	vote_uniquer_impl{ std::make_unique<lumex::vote_uniquer> () },
	vote_uniquer{ *vote_uniquer_impl },
	vote_cache_impl{ std::make_unique<lumex::vote_cache> (config.vote_cache, stats) },
	vote_cache{ *vote_cache_impl },
	vote_router_impl{ std::make_unique<lumex::vote_router> (vote_cache, active.recently_confirmed) },
	vote_router{ *vote_router_impl },
	vote_processor_impl{ std::make_unique<lumex::vote_processor> (config.vote_processor, vote_router, observers, stats, flags, logger, online_reps, rep_crawler, ledger, network_params, rep_tiers) },
	vote_processor{ *vote_processor_impl },
	vote_cache_processor_impl{ std::make_unique<lumex::vote_cache_processor> (config.vote_processor, vote_router, vote_cache, stats, logger) },
	vote_cache_processor{ *vote_cache_processor_impl },
	voting_policy_impl{ std::make_unique<lumex::voting_policy> (ledger) },
	voting_policy{ *voting_policy_impl },
	vote_generator_impl{ std::make_unique<lumex::vote_generator> (config.vote_generator, voting_policy, ledger, wallets, vote_processor, network, stats, logger, loopback_channel) },
	vote_generator{ *vote_generator_impl },
	scheduler_impl{ std::make_unique<lumex::scheduler::component> (config, *this, ledger, ledger_notifications, bucketing, active, online_reps, vote_cache, cementing_set, stats, logger) },
	scheduler{ *scheduler_impl },
	vote_replier_impl{ std::make_unique<lumex::vote_replier> (config.vote_replier, voting_policy, ledger, wallets, network_params.network, stats, logger, config.enable_voting) },
	vote_replier{ *vote_replier_impl },
	backlog_scan_impl{ std::make_unique<lumex::backlog_scan> (config.backlog_scan, ledger, stats) },
	backlog_scan{ *backlog_scan_impl },
	backlog_impl{ std::make_unique<lumex::bounded_backlog> (config, *this, ledger, ledger_notifications, bucketing, backlog_scan, block_processor, cementing_set, stats, logger) },
	backlog{ *backlog_impl },
	bootstrap_server_impl{ std::make_unique<lumex::bootstrap_server> (config.bootstrap_server, store, ledger, network_params.network, stats) },
	bootstrap_server{ *bootstrap_server_impl },
	bootstrap_impl{ std::make_unique<lumex::bootstrap_service> (config, ledger, ledger_notifications, block_processor, network, stats, logger) },
	bootstrap{ *bootstrap_impl },
	websocket_impl{ std::make_unique<lumex::websocket_server> (config.websocket_config, *this, observers, wallets, ledger, io_ctx, logger) },
	websocket{ *websocket_impl },
	epoch_upgrader_impl{ std::make_unique<lumex::epoch_upgrader> (*this, ledger, store, network_params, logger) },
	epoch_upgrader{ *epoch_upgrader_impl },
	local_block_broadcaster_impl{ std::make_unique<lumex::local_block_broadcaster> (config.local_block_broadcaster, *this, ledger_notifications, network, cementing_set, stats, logger) },
	local_block_broadcaster{ *local_block_broadcaster_impl },
	peer_history_impl{ std::make_unique<lumex::peer_history> (config.peer_history, store, network, logger, stats) },
	peer_history{ *peer_history_impl },
	monitor_impl{ std::make_unique<lumex::monitor> (config.monitor, *this) },
	monitor{ *monitor_impl },
	http_callbacks_impl{ std::make_unique<lumex::http_callbacks> (*this) },
	http_callbacks{ *http_callbacks_impl },
	pruning_impl{ std::make_unique<lumex::pruning> (config, flags, ledger, stats, logger) },
	pruning{ *pruning_impl },
	vote_rebroadcaster_impl{ std::make_unique<lumex::vote_rebroadcaster> (config.vote_rebroadcaster, flags, ledger, vote_router, network, wallets, rep_tiers, stats, logger) },
	vote_rebroadcaster{ *vote_rebroadcaster_impl },
	block_rebroadcaster_impl{ std::make_unique<lumex::block_rebroadcaster> (config.block_rebroadcaster, flags, active, network, stats, logger) },
	block_rebroadcaster{ *block_rebroadcaster_impl },
	startup_time{ std::chrono::steady_clock::now () },
	node_seq{ seq }
{
	logger.debug (lumex::log::type::node, "Constructing node...");

	vote_cache.rep_weight_query = [this] (lumex::account const & rep) {
		return ledger.weight (rep);
	};

	// Prioritize bootstrapping accounts with stale elections to find alternative forks
	active.election_stale.add ([this] (auto const & election) {
		bootstrap.prioritize (election->account);
	});

	// TODO: Hook this direclty in the schedulers
	backlog_scan.batch_activated.add ([this] (auto const & batch) {
		auto transaction = ledger.tx_begin_read ();
		for (auto const & info : batch)
		{
			scheduler.optimistic.activate (info.account, info.account_info, info.conf_info);
			scheduler.priority.activate (transaction, info.account, info.account_info, info.conf_info);
		}
	});

	// Do some cleanup due to this block never being processed by confirmation height processor
	cementing_set.cementing_failed.add ([this] (auto const & hash) {
		active.recently_confirmed.erase (hash);
	});

	// Cache forks
	ledger_notifications.blocks_processed.add ([this] (auto const & batch) {
		for (auto const & [result, context] : batch)
		{
			if (result == lumex::block_status::fork)
			{
				fork_cache.put (context.block);
			}
		}
	});

	// Announce new blocks via websocket
	ledger_notifications.blocks_processed.add ([this] (auto const & batch) {
		auto const transaction = ledger.tx_begin_read ();
		for (auto const & [result, context] : batch)
		{
			if (result == lumex::block_status::progress)
			{
				if (websocket.server && websocket.server->any_subscriber (lumex::websocket::topic::new_unconfirmed_block))
				{
					websocket.server->broadcast (lumex::websocket::message_builder (ledger).new_block_arrived (*context.block));
				}
			}
		}
	});

	// Do some cleanup of rolled back blocks
	ledger_notifications.blocks_rolled_back.add ([this] (auto const & blocks, auto const & rollback_root) {
		for (auto const & block : blocks)
		{
			history.erase (block->root ());
		}
	});

	// Representative is defined as online if replying to live votes or rep crawler queries
	observers.vote.add ([this] (std::shared_ptr<lumex::vote> const & vote, std::shared_ptr<lumex::transport::channel> const & channel, lumex::vote_source source, lumex::vote_code code) {
		release_assert (vote != nullptr);
		release_assert (channel != nullptr);
		debug_assert (code != lumex::vote_code::invalid);

		// Track rep weight voting on live elections
		bool should_observe = (code != lumex::vote_code::indeterminate);

		// Ignore republished votes when rep crawling
		if (source == lumex::vote_source::live)
		{
			should_observe |= rep_crawler.process (vote, channel);
		}

		if (should_observe)
		{
			online_reps.observe (vote->account);
		}
	});

	wallets.observer = [this] (bool active) {
		observers.wallet.notify (active);
	};

	network.disconnect_observer = [this] () {
		observers.disconnect.notify ();
	};

	observers.channel_connected.add ([this] (std::shared_ptr<lumex::transport::channel> const & channel) {
		network.send_keepalive_self (channel);
	});

	// Cancelling local work generation
	observers.work_cancel.add ([this] (lumex::root const & root_a) {
		this->work.cancel (root_a);
		this->distributed_work.cancel (root_a);
	});

	auto const network_label = network_params.network.get_current_network_as_string ();

	logger.info (lumex::log::type::node, "Version: {}", LUMEX_VERSION_STRING);
	logger.info (lumex::log::type::node, "Build information: {}", BUILD_INFO);
	logger.info (lumex::log::type::node, "Active network: {}", network_label);
	logger.info (lumex::log::type::node, "Database backend: {}", store.get_vendor ());
	logger.info (lumex::log::type::node, "Data path: {}", application_path.string ());
	logger.info (lumex::log::type::node, "Ledger path: {}", store.get_database_path ().string ());
	logger.info (lumex::log::type::node, "Work pool threads: {} ({})", work.threads.size (), (work.opencl ? "OpenCL" : "CPU"));
	logger.info (lumex::log::type::node, "Work peers: {}", config.work_peers.size ());
	logger.info (lumex::log::type::node, "Node ID: {}", lumex::log::as_node_id (node_id.pub));
	logger.info (lumex::log::type::node, "Number of buckets: {}", bucketing.size ());
	logger.info (lumex::log::type::node, "Genesis block: {}", config.network_params.ledger.genesis->hash ());
	logger.info (lumex::log::type::node, "Genesis account: {}", config.network_params.ledger.genesis->account ());

	if (!work_generation_enabled ())
	{
		logger.warn (lumex::log::type::node, "Work generation is disabled");
	}

	{
		auto [limit, burst_ratio] = outbound_limiter.get_limit ();
		logger.info (lumex::log::type::node, "Outbound bandwidth limit: {}, burst ratio: {}",
		limit == 0 ? "unlimited" : std::to_string (limit) + " bytes/s",
		burst_ratio);
	}

	if (!block_or_pruned_exists (config.network_params.ledger.genesis->hash ()))
	{
		logger.critical (lumex::log::type::node, "Genesis block not found. This commonly indicates a configuration issue, check that the --network or --data_path command line arguments are correct, and also the ledger backend node config option. If using a read-only CLI command a ledger must already exist, start the node with --daemon first.");

		if (network_params.network.is_beta_network ())
		{
			logger.critical (lumex::log::type::node, "Beta network may have reset, try clearing database files");
		}

		std::exit (1);
	}

	auto reps = wallets.reps ();
	if (reps.half_principal)
	{
		logger.info (lumex::log::type::node, "Found {} local representatives in wallets", reps.accounts.size ());

		for (auto const & account : reps.accounts)
		{
			logger.info (lumex::log::type::node, "Local representative: {}", account);
		}
	}

	if (flags.enable_voting)
	{
		config.enable_voting = true;
	}

	if (config.enable_voting)
	{
		logger.info (lumex::log::type::node, "Voting is enabled, more system resources will be used, local representatives: {}", reps.accounts.size ());
		if (reps.accounts.size () > 1)
		{
			logger.warn (lumex::log::type::node, "Voting with more than one representative can limit performance");
		}
	}
	else if (reps.half_principal)
	{
		logger.warn (lumex::log::type::node, "Found local representatives in wallets, but voting is disabled. To enable voting, set `[node] enable_voting=true` in the `config-node.toml` file or use `--enable_voting` command line argument");
	}

	if (flags.super_rebroadcaster)
	{
		logger.warn (lumex::log::type::node, "Super rebroadcaster mode enabled - broadcasting to all peers (expect high bandwidth usage)");
	}

	if ((network_params.network.is_live_network () || network_params.network.is_beta_network ()) && !flags.inactive_node)
	{
		ledger.bootstrap_weights = get_bootstrap_weights ();

		logger.info (lumex::log::type::node, "Initial bootstrap height: {:>10}", ledger.bootstrap_weights.max_blocks);
		logger.info (lumex::log::type::node, "Current ledger height:    {:>10}", ledger.block_count ());

		// Use bootstrap weights if initial bootstrap is not completed
		const bool use_bootstrap_weight = !ledger.bootstrap_height_reached ();
		if (use_bootstrap_weight)
		{
			logger.info (lumex::log::type::node, "Using predefined representative weights, since block count is less than bootstrap threshold");
			logger.info (lumex::log::type::node, "******************************************** Bootstrap weights ********************************************");

			// Sort the weights
			std::vector<std::pair<lumex::account, lumex::uint128_t>> sorted_weights (ledger.bootstrap_weights.representatives.begin (), ledger.bootstrap_weights.representatives.end ());
			std::sort (sorted_weights.begin (), sorted_weights.end (), [] (auto const & entry1, auto const & entry2) {
				return entry1.second > entry2.second;
			});

			for (auto const & rep : sorted_weights)
			{
				logger.info (lumex::log::type::node, "Using bootstrap rep weight: {} -> {}",
				rep.first,
				lumex::log::as_lumex (rep.second));
			}

			logger.info (lumex::log::type::node, "******************************************** ================= ********************************************");
		}
	}

	ledger.pruning = flags.enable_pruning || store.pruned.count (store.tx_begin_read ()) > 0;

	if (ledger.pruning)
	{
		// Gated on !flags.inactive_node so CLI utilities can still open the ledger
		if (config.enable_voting && !flags.inactive_node)
		{
			logger.critical (lumex::log::type::node, "Incompatibility detected between config node.enable_voting and existing pruned blocks");
			std::exit (1);
		}
		if (!flags.enable_pruning && !flags.inactive_node)
		{
			logger.critical (lumex::log::type::node, "To start node with existing pruned blocks use launch flag --enable_pruning");
			std::exit (1);
		}
		if (ledger.flags.topo_index && !flags.inactive_node)
		{
			logger.critical (lumex::log::type::node, "Incompatibility detected between topological index and ledger pruning. To proceed, either disable pruning, or run the node with --drop_topo_index to remove the topology index.");
			std::exit (1);
		}

		logger.warn (lumex::log::type::node, "WARNING: Ledger pruning is enabled. This feature is experimental and may result in node instability! Please see release notes for more information.");
	}

	cementing_set.cemented_observers.add ([this] (auto const & block) {
		// TODO: Is it neccessary to call this for all blocks?
		if (block->is_send ())
		{
			wallet_workers.post ([this, hash = block->hash (), destination = block->destination ()] () {
				wallets.receive_confirmed (hash, destination);
			});
		}
	});
}

lumex::node::~node ()
{
	logger.debug (lumex::log::type::node, "Destructing node...");
	stop ();
}

void lumex::node::inbound (const lumex::messages::message & message, const std::shared_ptr<lumex::transport::channel> & channel)
{
	debug_assert (channel->owner () == shared_from_this ()); // This node should be the channel owner

	debug_assert (message.header.network == network_params.network.current_network);
	debug_assert (message.header.version_using >= network_params.network.protocol_version_min);

	message_processor.process (message, channel);
}

void lumex::node::process_active (std::shared_ptr<lumex::block> const & incoming)
{
	block_processor.add (incoming, lumex::block_source::live);
}

void lumex::node::process_active (std::shared_ptr<lumex::vote> const & vote)
{
	vote_processor.vote (vote, loopback_channel, lumex::vote_source::live);
}

[[nodiscard]] lumex::block_status lumex::node::process (secure::write_transaction const & transaction, std::shared_ptr<lumex::block> block)
{
	auto status = ledger.process (transaction, block);
	logger.debug (lumex::log::type::node, "Directly processed block: {} (status: {})", block->hash (), status);
	return status;
}

lumex::block_status lumex::node::process (std::shared_ptr<lumex::block> block)
{
	auto const transaction = ledger.tx_begin_write (lumex::store::writer::node);
	return process (transaction, block);
}

std::optional<lumex::block_status> lumex::node::process_local (std::shared_ptr<lumex::block> const & block_a)
{
	return block_processor.add_blocking (block_a, lumex::block_source::local);
}

void lumex::node::process_local_async (std::shared_ptr<lumex::block> const & block_a)
{
	block_processor.add (block_a, lumex::block_source::local);
}

void lumex::node::start ()
{
	network.start ();
	message_processor.start ();

	if (!flags.disable_rep_crawler)
	{
		rep_crawler.start ();
	}

	bool tcp_enabled = false;
	if (!(flags.disable_bootstrap_listener && flags.disable_tcp_realtime))
	{
		tcp_listener.start ();
		tcp_enabled = true;

		if (network.port != tcp_listener.endpoint ().port ())
		{
			network.port = tcp_listener.endpoint ().port ();
		}

		logger.info (lumex::log::type::node, "Peering port: {}", network.port.load ());
	}
	else
	{
		logger.warn (lumex::log::type::node, "Peering is disabled");
	}

	if (!flags.disable_backup)
	{
		backup_wallet ();
	}
	// Start port mapping if external address is not defined and TCP ports are enabled
	if (config.external_address == boost::asio::ip::address_v6::any ().to_string () && tcp_enabled)
	{
		port_mapping.start ();
	}
	unchecked.start ();
	wallets.start ();
	rep_tiers.start ();
	vote_processor.start ();
	vote_cache_processor.start ();
	ledger_notifications.start ();
	block_processor.start ();
	active.start ();
	vote_generator.start ();
	cementing_set.start ();
	scheduler.start ();
	vote_replier.start ();
	backlog_scan.start ();
	backlog.start ();
	bootstrap_server.start ();
	bootstrap.start ();
	websocket.start ();
	telemetry.start ();
	stats.start ();
	local_block_broadcaster.start ();
	peer_history.start ();
	vote_router.start ();
	online_reps.start ();
	monitor.start ();
	http_callbacks.start ();
	pruning.start ();
	vote_rebroadcaster.start ();
	block_rebroadcaster.start ();

	add_initial_peers ();
}

void lumex::node::stop ()
{
	// Ensure stop can only be called once
	if (stopped.exchange (true))
	{
		return;
	}

	logger.info (lumex::log::type::node, "Stopping...");

	tcp_listener.stop ();
	online_reps.stop ();
	vote_router.stop ();
	peer_history.stop ();
	// Cancels ongoing work generation tasks, which may be blocking other threads
	// No tasks may wait for work generation in I/O threads, or termination signal capturing will be unable to call node::stop()
	distributed_work.stop ();
	backlog_scan.stop ();
	bootstrap.stop ();
	backlog.stop ();
	rep_crawler.stop ();
	unchecked.stop ();
	block_processor.stop ();
	vote_replier.stop ();
	vote_cache_processor.stop ();
	vote_processor.stop ();
	rep_tiers.stop ();
	scheduler.stop ();
	active.stop ();
	vote_generator.stop ();
	cementing_set.stop ();
	ledger_notifications.stop ();
	telemetry.stop ();
	websocket.stop ();
	bootstrap_server.stop ();
	port_mapping.stop ();
	wallets.stop ();
	stats.stop ();
	epoch_upgrader.stop ();
	local_block_broadcaster.stop ();
	message_processor.stop ();
	network.stop ();
	monitor.stop ();
	http_callbacks.stop ();
	pruning.stop ();
	vote_rebroadcaster.stop ();
	block_rebroadcaster.stop ();

	bootstrap_workers.stop ();
	wallet_workers.stop ();
	election_workers.stop ();
	workers.stop ();

	// work pool is not stopped on purpose due to testing setup

	// Stop the IO runner last
	runner.abort (); // TODO: Remove this
	runner.join ();
	debug_assert (io_ctx_shared.use_count () == 1); // Node should be the last user of the io_context
}

lumex::block_hash lumex::node::latest (lumex::account const & account_a)
{
	return ledger.any.account_head (ledger.tx_begin_read (), account_a);
}

lumex::uint128_t lumex::node::balance (lumex::account const & account_a)
{
	return ledger.any.account_balance (ledger.tx_begin_read (), account_a).value_or (0).number ();
}

std::shared_ptr<lumex::block> lumex::node::block (lumex::block_hash const & hash_a)
{
	return ledger.any.block_get (ledger.tx_begin_read (), hash_a);
}

bool lumex::node::block_or_pruned_exists (lumex::block_hash const & hash_a) const
{
	return ledger.any.block_exists_or_pruned (ledger.tx_begin_read (), hash_a);
}

std::pair<lumex::uint128_t, lumex::uint128_t> lumex::node::balance_pending (lumex::account const & account_a, bool only_confirmed_a)
{
	std::pair<lumex::uint128_t, lumex::uint128_t> result;
	auto const transaction = ledger.tx_begin_read ();
	result.first = only_confirmed_a ? ledger.cemented.account_balance (transaction, account_a).value_or (0).number () : ledger.any.account_balance (transaction, account_a).value_or (0).number ();
	result.second = ledger.account_receivable (transaction, account_a, only_confirmed_a);
	return result;
}

lumex::uint128_t lumex::node::weight (lumex::account const & account_a)
{
	auto txn = ledger.tx_begin_read ();
	return ledger.weight_exact (txn, account_a);
}

lumex::uint128_t lumex::node::minimum_principal_weight ()
{
	return online_reps.trended () / network_params.network.principal_weight_factor;
}

void lumex::node::backup_wallet ()
{
	for (auto const & [id, wallet] : wallets.all_wallets ())
	{
		boost::system::error_code error_chmod;
		auto backup_path (application_path / "backup");

		std::filesystem::create_directories (backup_path);
		lumex::set_secure_perm_directory (backup_path, error_chmod);
		wallet->write_backup (backup_path / (id.to_string () + ".json"));
	}
	auto this_l (shared ());
	workers.post_delayed (network_params.node.backup_interval, [this_l] () {
		this_l->backup_wallet ();
	});
}

uint64_t lumex::node::default_difficulty (lumex::work_version const version_a) const
{
	uint64_t result{ std::numeric_limits<uint64_t>::max () };
	switch (version_a)
	{
		case lumex::work_version::work_1:
			result = network_params.work.threshold_base (version_a);
			break;
		default:
			debug_assert (false && "Invalid version specified to default_difficulty");
	}
	return result;
}

uint64_t lumex::node::default_receive_difficulty (lumex::work_version const version_a) const
{
	uint64_t result{ std::numeric_limits<uint64_t>::max () };
	switch (version_a)
	{
		case lumex::work_version::work_1:
			result = network_params.work.epoch_2_receive;
			break;
		default:
			debug_assert (false && "Invalid version specified to default_receive_difficulty");
	}
	return result;
}

uint64_t lumex::node::max_work_generate_difficulty (lumex::work_version const version_a) const
{
	return lumex::difficulty::from_multiplier (config.max_work_generate_multiplier, default_difficulty (version_a));
}

bool lumex::node::local_work_generation_enabled () const
{
	return config.work_threads > 0 || work.opencl;
}

bool lumex::node::work_generation_enabled () const
{
	return work_generation_enabled (config.work_peers);
}

bool lumex::node::work_generation_enabled (std::vector<std::pair<std::string, uint16_t>> const & peers_a) const
{
	return !peers_a.empty () || local_work_generation_enabled ();
}

std::optional<uint64_t> lumex::node::work_generate_blocking (lumex::block & block_a, uint64_t difficulty_a)
{
	auto opt_work_l (work_generate_blocking (block_a.work_version (), block_a.root (), difficulty_a, block_a.account_field ()));
	if (opt_work_l.has_value ())
	{
		block_a.block_work_set (opt_work_l.value ());
	}
	return opt_work_l;
}

void lumex::node::work_generate (lumex::work_version const version_a, lumex::root const & root_a, uint64_t difficulty_a, std::function<void (std::optional<uint64_t>)> callback_a, std::optional<lumex::account> const & account_a, bool secondary_work_peers_a)
{
	auto const & peers_l (secondary_work_peers_a ? config.secondary_work_peers : config.work_peers);
	if (distributed_work.make (version_a, root_a, peers_l, difficulty_a, callback_a, account_a))
	{
		// Error in creating the job (either stopped or work generation is not possible)
		callback_a (std::nullopt);
	}
}

std::optional<uint64_t> lumex::node::work_generate_blocking (lumex::work_version const version_a, lumex::root const & root_a, uint64_t difficulty_a, std::optional<lumex::account> const & account_a)
{
	std::promise<std::optional<uint64_t>> promise;
	work_generate (
	version_a, root_a, difficulty_a, [&promise] (std::optional<uint64_t> opt_work_a) {
		promise.set_value (opt_work_a);
	},
	account_a);
	return promise.get_future ().get ();
}

std::optional<uint64_t> lumex::node::work_generate_blocking (lumex::block & block_a)
{
	debug_assert (network_params.network.is_dev_network ());
	return work_generate_blocking (block_a, default_difficulty (lumex::work_version::work_1));
}

std::optional<uint64_t> lumex::node::work_generate_blocking (lumex::root const & root_a)
{
	debug_assert (network_params.network.is_dev_network ());
	return work_generate_blocking (root_a, default_difficulty (lumex::work_version::work_1));
}

std::optional<uint64_t> lumex::node::work_generate_blocking (lumex::root const & root_a, uint64_t difficulty_a)
{
	debug_assert (network_params.network.is_dev_network ());
	return work_generate_blocking (lumex::work_version::work_1, root_a, difficulty_a);
}

void lumex::node::add_initial_peers ()
{
	if (flags.disable_add_initial_peers)
	{
		logger.warn (lumex::log::type::node, "Not adding initial peers because `disable_add_initial_peers` flag is set");
		return;
	}

	auto initial_peers = peer_history.peers ();

	logger.info (lumex::log::type::node, "Adding cached initial peers: {}", initial_peers.size ());

	for (auto const & peer : initial_peers)
	{
		network.merge_peer (peer);
	}
}

void lumex::node::start_election (std::shared_ptr<lumex::block> const & block)
{
	scheduler.manual.push (block);
}

bool lumex::node::block_confirmed (lumex::block_hash const & hash)
{
	return ledger.cemented.block_exists_or_pruned (ledger.tx_begin_read (), hash);
}

bool lumex::node::block_confirmed_or_being_confirmed (lumex::secure::transaction const & transaction, lumex::block_hash const & hash)
{
	return cementing_set.contains (hash) || ledger.cemented.block_exists_or_pruned (transaction, hash);
}

bool lumex::node::block_confirmed_or_being_confirmed (lumex::block_hash const & hash_a)
{
	return block_confirmed_or_being_confirmed (ledger.tx_begin_read (), hash_a);
}

bool lumex::node::online () const
{
	return rep_crawler.total_weight () > online_reps.delta ();
}

bool lumex::node::warmed_up () const
{
	return std::chrono::steady_clock::now () - startup_time >= warmup_time;
}

std::shared_ptr<lumex::node> lumex::node::shared ()
{
	return shared_from_this ();
}

uint64_t lumex::node::store_version () const
{
	return store.version.get_version (store.tx_begin_read ());
}

lumex::node_capabilities_flags lumex::node::get_capabilities () const
{
	if (flags.capabilities_override)
	{
		return *flags.capabilities_override;
	}
	// TODO: Set capabilities flags based on node configuration and state
	lumex::node_capabilities_flags caps;
	return caps;
}

lumex::bootstrap_weights lumex::node::get_bootstrap_weights () const
{
	return lumex::get_bootstrap_weights (network_params.network.current_network);
}

void lumex::node::bootstrap_block (const lumex::block_hash & hash)
{
	// If we are running pruning node check if block was not already pruned
	if (!ledger.pruning || !store.pruned.exists (store.tx_begin_read (), hash))
	{
		// We don't have the block, try to bootstrap it
		// TODO: Use ascending bootstraper to bootstrap block hash
	}
}

uint64_t lumex::node::block_count () const
{
	return ledger.block_count ();
}

uint64_t lumex::node::cemented_count () const
{
	return ledger.cemented_count ();
}

lumex::account lumex::node::get_node_id () const
{
	return node_id.pub;
}

std::filesystem::path const & lumex::node::get_data_path () const
{
	return application_path;
}

lumex::messages::telemetry_data lumex::node::local_telemetry () const
{
	lumex::messages::telemetry_data telemetry_data;
	telemetry_data.node_id = node_id.pub;
	telemetry_data.block_count = ledger.block_count ();
	telemetry_data.cemented_count = ledger.cemented_count ();
	telemetry_data.bandwidth_cap = config.bandwidth_limit;
	telemetry_data.protocol_version = network_params.network.protocol_version;
	telemetry_data.uptime = std::chrono::duration_cast<std::chrono::seconds> (std::chrono::steady_clock::now () - startup_time).count ();
	telemetry_data.unchecked_count = unchecked.count ();
	telemetry_data.genesis_block = network_params.ledger.genesis->hash ();
	telemetry_data.peer_count = lumex::narrow_cast<decltype (telemetry_data.peer_count)> (network.size ());
	telemetry_data.account_count = ledger.account_count ();
	telemetry_data.major_version = lumex::get_major_node_version ();
	telemetry_data.minor_version = lumex::get_minor_node_version ();
	telemetry_data.patch_version = lumex::get_patch_node_version ();
	telemetry_data.pre_release_version = lumex::get_pre_release_node_version ();
	telemetry_data.maker = ledger.pruning ? lumex::messages::telemetry_maker::nf_pruned_node : lumex::messages::telemetry_maker::nf_node;
	telemetry_data.timestamp = std::chrono::system_clock::now ();
	telemetry_data.active_difficulty = default_difficulty (lumex::work_version::work_1);
	telemetry_data.database_backend = lumex::messages::to_telemetry_database_backend (config.database_backend);
	auto conf_latency = active.recently_confirmed.latency_percentiles ();
	telemetry_data.confirmation_latency_ms_p50 = conf_latency.p50;
	telemetry_data.confirmation_latency_ms_p90 = conf_latency.p90;
	telemetry_data.confirmation_latency_ms_p99 = conf_latency.p99;
	auto bs = bootstrap.status ();
	telemetry_data.bootstrap_status = bs.priorities == 0
	? lumex::messages::telemetry_bootstrap_status::synced
	: lumex::messages::telemetry_bootstrap_status::syncing;
	// Make sure this is the final operation!
	telemetry_data.sign (node_id);
	return telemetry_data;
}

std::string lumex::node::identifier () const
{
	return make_logger_identifier (node_id);
}

std::string lumex::node::make_logger_identifier (const lumex::keypair & node_id)
{
	// Node identifier consists of first 10 characters of node id
	return node_id.pub.to_node_id ().substr (0, 10);
}

lumex::container_info lumex::node::container_info () const
{
	/*
	 * TODO: Add container infos for:
	 * - bootstrap_server
	 * - peer_history
	 * - port_mapping
	 * - epoch_upgrader
	 * - websocket
	 */

	lumex::container_info info;
	info.add ("work", work.container_info ());
	info.add ("ledger", ledger.container_info ());
	info.add ("active", active.container_info ());
	info.add ("tcp_listener", tcp_listener.container_info ());
	info.add ("network", network.container_info ());
	info.add ("telemetry", telemetry.container_info ());
	info.add ("workers", workers.container_info ());
	info.add ("bootstrap_workers", bootstrap_workers.container_info ());
	info.add ("wallet_workers", wallet_workers.container_info ());
	info.add ("election_workers", election_workers.container_info ());
	info.add ("observers", observers.container_info ());
	info.add ("wallets", wallets.container_info ());
	info.add ("vote_processor", vote_processor.container_info ());
	info.add ("vote_cache_processor", vote_cache_processor.container_info ());
	info.add ("rep_crawler", rep_crawler.container_info ());
	info.add ("block_processor", block_processor.container_info ());
	info.add ("online_reps", online_reps.container_info ());
	info.add ("history", history.container_info ());
	info.add ("block_uniquer", block_uniquer.container_info ());
	info.add ("vote_uniquer", vote_uniquer.container_info ());
	info.add ("cementing_set", cementing_set.container_info ());
	info.add ("distributed_work", distributed_work.container_info ());
	info.add ("vote_replier", vote_replier.container_info ());
	info.add ("scheduler", scheduler.container_info ());
	info.add ("vote_cache", vote_cache.container_info ());
	info.add ("vote_router", vote_router.container_info ());
	info.add ("vote_generator", vote_generator.container_info ());
	info.add ("bootstrap", bootstrap.container_info ());
	info.add ("unchecked", unchecked.container_info ());
	info.add ("local_block_broadcaster", local_block_broadcaster.container_info ());
	info.add ("rep_tiers", rep_tiers.container_info ());
	info.add ("message_processor", message_processor.container_info ());
	info.add ("bandwidth", outbound_limiter.container_info ());
	info.add ("backlog_scan", backlog_scan.container_info ());
	info.add ("bounded_backlog", backlog.container_info ());
	info.add ("http_callbacks", http_callbacks.container_info ());
	info.add ("pruning", pruning.container_info ());
	info.add ("vote_rebroadcaster", vote_rebroadcaster.container_info ());
	info.add ("fork_cache", fork_cache.container_info ());
	return info;
}

/*
 *
 */

void lumex::node::copy_with_compaction (std::filesystem::path const & destination)
{
	store.backend.copy_with_compaction (destination);
}

/*
 *
 */

lumex::keypair lumex::load_or_create_node_id (std::filesystem::path const & application_path)
{
	auto & logger = lumex::default_logger ();

	logger.info (lumex::log::type::init, "Using data directory: {}", application_path.string ());

	auto node_private_key_path = application_path / "node_id_private.key";
	std::ifstream ifs (node_private_key_path.c_str ());
	if (ifs.good ())
	{
		std::string node_private_key;
		ifs >> node_private_key;
		release_assert (node_private_key.size () == 64);
		lumex::keypair kp = lumex::keypair (node_private_key);

		logger.info (lumex::log::type::init, "Loaded local node ID: {}",
		kp.pub.to_node_id ());

		return kp;
	}
	else
	{
		lumex::keypair kp;
		std::ofstream ofs (node_private_key_path.c_str (), std::ofstream::out | std::ofstream::trunc);
		ofs << kp.prv.to_string () << std::endl
			<< std::flush;
		ofs.close ();
		release_assert (!ofs.fail ());

		logger.info (lumex::log::type::init, "Generated new local node ID: {}",
		kp.pub.to_node_id ());

		return kp;
	}
}
