#include <lumex/lib/files.hpp>
#include <lumex/lib/lmdbconfig.hpp>
#include <lumex/lib/rpcconfig.hpp>
#include <lumex/lib/thread_runner.hpp>
#include <lumex/node/block_processor.hpp>
#include <lumex/node/bootstrap/bootstrap_config.hpp>
#include <lumex/node/bootstrap/bootstrap_server.hpp>
#include <lumex/node/bootstrap/bootstrap_service.hpp>
#include <lumex/node/ipc/ipc_config.hpp>
#include <lumex/node/ipc/ipc_server.hpp>
#include <lumex/node/json_handler.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/transport/transport.hpp>
#include <lumex/node/unchecked_map.hpp>
#include <lumex/rpc/rpc.hpp>
#include <lumex/rpc/rpc_request_processor.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/test_common/network.hpp>
#include <lumex/test_common/rate_observer.hpp>
#include <lumex/test_common/system.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <thread>

using namespace std::chrono_literals;

namespace
{
void wait_for_key ()
{
	int junk;
	std::cin >> junk;
}

class rpc_wrapper
{
public:
	rpc_wrapper (lumex::test::system & system, lumex::node & node, uint16_t port) :
		node_rpc_config{},
		rpc_config{ node.network_params.network, port, true },
		ipc{ node, node_rpc_config },
		ipc_rpc_processor{ system.io_ctx, rpc_config },
		rpc{ system.io_ctx, rpc_config, ipc_rpc_processor }
	{
	}

	void start ()
	{
		rpc.start ();
	}

public:
	lumex::node_rpc_config node_rpc_config;
	lumex::rpc_config rpc_config;
	lumex::ipc::ipc_server ipc;
	lumex::ipc_rpc_processor ipc_rpc_processor;
	lumex::rpc rpc;
};

std::unique_ptr<rpc_wrapper> start_rpc (lumex::test::system & system, lumex::node & node, uint16_t port)
{
	auto rpc = std::make_unique<rpc_wrapper> (system, node, port);
	rpc->start ();
	return rpc;
}
}

TEST (bootstrap, profile)
{
	lumex::test::system system;
	lumex::thread_runner runner{ system.io_ctx, system.logger, 2 };
	lumex::network_type network = lumex::network_type::lumex_beta_network;
	lumex::network_params network_params{ network };

	// Set up client and server nodes
	lumex::node_config config_server{ network_params };
	config_server.preconfigured_peers.clear ();
	config_server.bandwidth_limit = 0; // Unlimited server bandwidth
	config_server.bootstrap->enable = false;
	lumex::node_flags flags_server;
	flags_server.disable_legacy_bootstrap = true;
	flags_server.disable_wallet_bootstrap = true;
	flags_server.disable_add_initial_peers = true;
	flags_server.disable_ongoing_bootstrap = true;
	auto data_path_server = lumex::working_path (network);
	// auto data_path_server = "";
	auto server = std::make_shared<lumex::node> (data_path_server, config_server, system.work, flags_server);
	system.nodes.push_back (server);
	server->start ();

	lumex::node_config config_client{ network_params };
	config_client.preconfigured_peers.clear ();
	config_client.bandwidth_limit = 0; // Unlimited server bandwidth
	lumex::node_flags flags_client;
	flags_client.disable_legacy_bootstrap = true;
	flags_client.disable_wallet_bootstrap = true;
	flags_client.disable_add_initial_peers = true;
	flags_client.disable_ongoing_bootstrap = true;
	config_client.ipc_config->transport_tcp.enabled = true;
	// Disable database integrity safety for higher throughput
	config_client.lmdb_config->sync = lumex::lmdb_config::sync_strategy::nosync_unsafe;
	// auto client = system.add_node (config_client, flags_client);

	// macos 16GB RAM disk:  diskutil erasevolume HFS+ "RAMDisk" `hdiutil attach -nomount ram://33554432`
	// auto data_path_client = "/Volumes/RAMDisk";
	auto data_path_client = lumex::unique_path ();
	auto client = std::make_shared<lumex::node> (data_path_client, config_client, system.work, flags_client);
	system.nodes.push_back (client);
	client->start ();

	// Set up RPC
	auto client_rpc = start_rpc (system, *server, 55000);
	auto server_rpc = start_rpc (system, *client, 55001);

	lumex::mutex mutex;

	std::cout << "server count: " << server->ledger.block_count () << std::endl;

	lumex::test::rate_observer rate;
	rate.observe ("count", [&] () { return client->ledger.block_count (); });
	rate.observe ("unchecked", [&] () { return client->unchecked.count (); });
	rate.observe ("block_processor", [&] () { return client->block_processor.size (); });
	rate.observe ("priority", [&] () { return client->bootstrap.priority_size (); });
	rate.observe ("blocking", [&] () { return client->bootstrap.blocked_size (); });
	rate.observe (*client, lumex::stat::type::bootstrap, lumex::stat::detail::request, lumex::stat::dir::out);
	rate.observe (*client, lumex::stat::type::bootstrap, lumex::stat::detail::reply, lumex::stat::dir::in);
	rate.observe (*client, lumex::stat::type::bootstrap, lumex::stat::detail::blocks, lumex::stat::dir::in);
	rate.observe (*server, lumex::stat::type::bootstrap_server, lumex::stat::detail::blocks, lumex::stat::dir::out);
	rate.observe (*client, lumex::stat::type::ledger, lumex::stat::detail::old, lumex::stat::dir::in);
	rate.observe (*client, lumex::stat::type::ledger, lumex::stat::detail::gap_epoch_open_pending, lumex::stat::dir::in);
	rate.observe (*client, lumex::stat::type::ledger, lumex::stat::detail::gap_source, lumex::stat::dir::in);
	rate.observe (*client, lumex::stat::type::ledger, lumex::stat::detail::gap_previous, lumex::stat::dir::in);
	rate.background_print (3s);

	// wait_for_key ();
	while (true)
	{
		lumex::test::establish_tcp (system, *client, server->network.endpoint ());
		std::this_thread::sleep_for (10s);
	}

	server->stop ();
	client->stop ();
}
