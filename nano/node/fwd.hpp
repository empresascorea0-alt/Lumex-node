#pragma once

#include <nano/lib/fwd.hpp>
#include <nano/node/transport/fwd.hpp>
#include <nano/secure/fwd.hpp>
#include <nano/store/fwd.hpp>

namespace nano
{
class account_sets_config;
class active_elections;
class backlog_scan;
class bandwidth_limiter;
class block_processor;
class block_rebroadcaster;
class bounded_backlog;
class bucketing;
class bootstrap_config;
class bootstrap_server;
class bootstrap_service;
class cementing_set;
class distributed_work_factory;
class election;
class election_status;
class epoch_upgrader;
class fork_cache;
class ledger_notifications;
class local_block_broadcaster;
class local_vote_history;
class logger;
class message_processor;
class monitor;
class network;
class network_params;
class node;
class node_config;
class node_flags;
class node_observers;
class online_reps;
class peer_history;
class port_mapping;
class pruning;
class recently_cemented_cache;
class recently_confirmed_cache;
class rep_crawler;
class rep_tiers;
class http_callbacks;
class telemetry;
class unchecked_map;
class stats;
class vote_cache;
class vote_cache_processor;
class vote_generator;
class vote_replier;
class vote_processor;
class vote_rebroadcaster;
class vote_router;
class vote_spacing;
class voting_policy;
class wallet;
class wallets;
class wallets_store;
class websocket_server;

enum class block_source;
enum class election_behavior;
enum class election_state;
enum class vote_code;
enum class vote_source;
}

namespace nano::bootstrap
{
class bootstrap_context;
}

namespace nano::scheduler
{
class component;
class hinted;
class manual;
class optimistic;
class priority;
}
