#include <lumex/lib/blockbuilders.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/config.hpp>
#include <lumex/lib/files.hpp>
#include <lumex/lib/vote.hpp>
#include <lumex/lib/work_version.hpp>
#include <lumex/messages/messages.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/backlog_scan.hpp>
#include <lumex/node/block_processor.hpp>
#include <lumex/node/block_rebroadcaster.hpp>
#include <lumex/node/bootstrap/bootstrap_config.hpp>
#include <lumex/node/bootstrap/bootstrap_service.hpp>
#include <lumex/node/cementing_set.hpp>
#include <lumex/node/election.hpp>
#include <lumex/node/inactive_node.hpp>
#include <lumex/node/local_block_broadcaster.hpp>
#include <lumex/node/local_vote_history.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/node_observers.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/online_reps.hpp>
#include <lumex/node/peer_history.hpp>
#include <lumex/node/portmapping.hpp>
#include <lumex/node/pruning.hpp>
#include <lumex/node/repcrawler.hpp>
#include <lumex/node/scheduler/component.hpp>
#include <lumex/node/scheduler/hinted.hpp>
#include <lumex/node/scheduler/manual.hpp>
#include <lumex/node/scheduler/optimistic.hpp>
#include <lumex/node/scheduler/priority.hpp>
#include <lumex/node/transport/fake.hpp>
#include <lumex/node/transport/inproc.hpp>
#include <lumex/node/transport/tcp_config.hpp>
#include <lumex/node/transport/tcp_listener.hpp>
#include <lumex/node/unchecked_map.hpp>
#include <lumex/node/vote_cache.hpp>
#include <lumex/node/vote_generator.hpp>
#include <lumex/node/vote_processor.hpp>
#include <lumex/node/vote_rebroadcaster.hpp>
#include <lumex/node/vote_replier.hpp>
#include <lumex/node/vote_router.hpp>
#include <lumex/node/wallet.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_set_any.hpp>
#include <lumex/secure/ledger_set_cemented.hpp>
#include <lumex/store/ledger/account.hpp>
#include <lumex/store/ledger/peer.hpp>
#include <lumex/store/ledger/pruned.hpp>
#include <lumex/test_common/chains.hpp>
#include <lumex/test_common/make_store.hpp>
#include <lumex/test_common/network.hpp>
#include <lumex/test_common/system.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <fstream>
#include <numeric>

using namespace std::chrono_literals;

TEST (node, null_account)
{
	auto const & null_account = lumex::account::null ();
	ASSERT_EQ (null_account, nullptr);
	ASSERT_FALSE (null_account != nullptr);

	lumex::account default_account{};
	ASSERT_FALSE (default_account == nullptr);
	ASSERT_NE (default_account, nullptr);
}

TEST (node, stop)
{
	lumex::test::system system (1);
	ASSERT_NE (system.nodes[0]->wallets.items.end (), system.nodes[0]->wallets.items.begin ());
	system.stop_node (*system.nodes[0]);
	ASSERT_TRUE (true);
}

TEST (node, work_generate)
{
	lumex::test::system system (1);
	auto & node (*system.nodes[0]);
	lumex::block_hash root{ 1 };
	lumex::work_version version{ lumex::work_version::work_1 };
	{
		auto difficulty = lumex::difficulty::from_multiplier (1.5, node.network_params.work.base);
		auto work = node.work_generate_blocking (version, root, difficulty);
		ASSERT_TRUE (work.has_value ());
		ASSERT_GE (lumex::dev::network_params.work.difficulty (version, root, work.value ()), difficulty);
	}
	{
		auto difficulty = lumex::difficulty::from_multiplier (0.5, node.network_params.work.base);
		std::optional<uint64_t> work;
		do
		{
			work = node.work_generate_blocking (version, root, difficulty);
		} while (lumex::dev::network_params.work.difficulty (version, root, work.value ()) >= node.network_params.work.base);
		ASSERT_TRUE (work.has_value ());
		ASSERT_GE (lumex::dev::network_params.work.difficulty (version, root, work.value ()), difficulty);
		ASSERT_FALSE (lumex::dev::network_params.work.difficulty (version, root, work.value ()) >= node.network_params.work.base);
	}
}

TEST (node, block_store_path_failure)
{
	lumex::test::system system;
	auto path (lumex::unique_path ());
	lumex::work_pool pool{ lumex::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	auto node (std::make_shared<lumex::node> (system.get_available_port (), path, pool, lumex::node_flags{}));
	system.register_node (node);
	ASSERT_TRUE (node->wallets.items.empty ());
}

TEST (node_DeathTest, readonly_block_store_not_exist)
{
	// This is a read-only node with no ledger file
	ASSERT_THROW (lumex::inactive_node (lumex::unique_path (), lumex::inactive_node_flag_defaults ()), std::runtime_error);
}

TEST (node, password_fanout)
{
	lumex::test::system system;
	lumex::node_config config;
	config.peering_port = system.get_available_port ();
	lumex::work_pool pool{ lumex::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	config.password_fanout = 10;
	auto & node = *system.add_node (config);
	auto wallet (node.wallets.create (100));
	ASSERT_EQ (10, wallet->store.password.values.size ());
}

TEST (node, balance)
{
	lumex::test::system system (1);
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	auto transaction = system.nodes[0]->ledger.tx_begin_write ();
	ASSERT_EQ (std::numeric_limits<lumex::uint128_t>::max (), system.nodes[0]->ledger.any.account_balance (transaction, lumex::dev::genesis_key.pub));
}

TEST (node, send_unkeyed)
{
	lumex::test::system system (1);
	lumex::keypair key2;
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	system.wallet (0)->store.password.value_set (lumex::keypair ().prv);
	ASSERT_EQ (nullptr, system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
}

TEST (node, send_self)
{
	lumex::test::system system (1);
	lumex::keypair key2;
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_TIMELY (10s, !system.nodes[0]->balance (key2.pub).is_zero ());
	ASSERT_EQ (std::numeric_limits<lumex::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (lumex::dev::genesis_key.pub));
}

TEST (node, send_single)
{
	lumex::test::system system (2);
	lumex::keypair key2;
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_EQ (std::numeric_limits<lumex::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (lumex::dev::genesis_key.pub));
	ASSERT_TRUE (system.nodes[0]->balance (key2.pub).is_zero ());
	ASSERT_TIMELY (10s, !system.nodes[0]->balance (key2.pub).is_zero ());
}

TEST (node, send_single_observing_peer)
{
	lumex::test::system system (3);
	lumex::keypair key2;
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_EQ (std::numeric_limits<lumex::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (lumex::dev::genesis_key.pub));
	ASSERT_TRUE (system.nodes[0]->balance (key2.pub).is_zero ());
	ASSERT_TIMELY (10s, std::all_of (system.nodes.begin (), system.nodes.end (), [&] (std::shared_ptr<lumex::node> const & node_a) { return !node_a->balance (key2.pub).is_zero (); }));
}

TEST (node, send_out_of_order)
{
	lumex::test::system system (2);
	auto & node1 (*system.nodes[0]);
	lumex::keypair key2;
	lumex::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<lumex::uint128_t>::max () - node1.config.receive_minimum.number ())
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .previous (send1->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<lumex::uint128_t>::max () - 2 * node1.config.receive_minimum.number ())
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	auto send3 = builder.make_block ()
				 .previous (send2->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<lumex::uint128_t>::max () - 3 * node1.config.receive_minimum.number ())
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (send2->hash ()))
				 .build ();
	node1.process_active (send3);
	node1.process_active (send2);
	node1.process_active (send1);
	ASSERT_TIMELY (10s, std::all_of (system.nodes.begin (), system.nodes.end (), [&] (std::shared_ptr<lumex::node> const & node_a) { return node_a->balance (lumex::dev::genesis_key.pub) == lumex::dev::constants.genesis_amount - node1.config.receive_minimum.number () * 3; }));
}

TEST (node, quick_confirm)
{
	lumex::test::system system (1);
	auto & node1 (*system.nodes[0]);
	lumex::keypair key;
	lumex::block_hash previous (node1.latest (lumex::dev::genesis_key.pub));
	auto genesis_start_balance (node1.balance (lumex::dev::genesis_key.pub));
	system.wallet (0)->insert_adhoc (key.prv);
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);

	// Wait for online weight to stabilize after wallet insertion
	ASSERT_TIMELY (5s, node1.online_reps.online () >= genesis_start_balance);
	auto delta = node1.online_reps.delta ();

	auto send = lumex::send_block_builder ()
				.previous (previous)
				.destination (key.pub)
				.balance (delta + 1)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*system.work.generate (previous))
				.build ();
	node1.process_active (send);
	ASSERT_TIMELY (10s, !node1.balance (key.pub).is_zero ());

	ASSERT_EQ (node1.balance (lumex::dev::genesis_key.pub), delta + 1);
	ASSERT_EQ (node1.balance (key.pub), genesis_start_balance - (delta + 1));
}

TEST (node, node_receive_quorum)
{
	lumex::test::system system (1);
	auto & node1 = *system.nodes[0];
	lumex::keypair key;
	lumex::block_hash previous (node1.latest (lumex::dev::genesis_key.pub));
	system.wallet (0)->insert_adhoc (key.prv);
	auto send = lumex::send_block_builder ()
				.previous (previous)
				.destination (key.pub)
				.balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*system.work.generate (previous))
				.build ();
	node1.process_active (send);
	ASSERT_TIMELY (10s, node1.block_or_pruned_exists (send->hash ()));
	ASSERT_TIMELY (10s, node1.active.election (lumex::qualified_root (previous, previous)) != nullptr);
	auto election (node1.active.election (lumex::qualified_root (previous, previous)));
	ASSERT_NE (nullptr, election);
	ASSERT_FALSE (election->confirmed ());
	ASSERT_EQ (1, election->votes ().size ());

	lumex::test::system system2;
	system2.add_node ();

	system2.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	ASSERT_TRUE (node1.balance (key.pub).is_zero ());
	node1.network.tcp_channels.start_tcp (system2.nodes[0]->network.endpoint ());
	while (node1.balance (key.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
		ASSERT_NO_ERROR (system2.poll ());
	}
}

TEST (node, auto_bootstrap)
{
	lumex::test::system system;
	lumex::node_config config;
	config.peering_port = system.get_available_port ();
	config.backlog_scan->enable = false;
	lumex::node_flags node_flags;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_lazy_bootstrap = true;
	auto node0 = system.add_node (config, node_flags);
	lumex::keypair key2;
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	auto send1 (system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key2.pub, node0->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send1);
	ASSERT_TIMELY_EQ (10s, node0->balance (key2.pub), node0->config.receive_minimum.number ());
	auto node1 (std::make_shared<lumex::node> (system.get_available_port (), lumex::unique_path (), system.work, node_flags));
	node1->start ();
	system.nodes.push_back (node1);
	ASSERT_NE (nullptr, lumex::test::establish_tcp (system, *node1, node0->network.endpoint ()));
	ASSERT_TIMELY_EQ (10s, node1->balance (key2.pub), node0->config.receive_minimum.number ());
	ASSERT_TRUE (node1->block_or_pruned_exists (send1->hash ()));
	// Wait block receive
	ASSERT_TIMELY_EQ (5s, node1->ledger.block_count (), 3);
	// Confirmation for all blocks
	ASSERT_TIMELY_EQ (5s, node1->ledger.cemented_count (), 3);
}

TEST (node, auto_bootstrap_reverse)
{
	lumex::test::system system;
	lumex::node_config config;
	config.peering_port = system.get_available_port ();
	config.backlog_scan->enable = false;
	lumex::node_flags node_flags;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_lazy_bootstrap = true;
	auto node0 = system.add_node (config, node_flags);
	lumex::keypair key2;
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	auto node1 (std::make_shared<lumex::node> (system.get_available_port (), lumex::unique_path (), system.work, node_flags));
	ASSERT_NE (nullptr, system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key2.pub, node0->config.receive_minimum.number ()));
	node1->start ();
	system.nodes.push_back (node1);
	ASSERT_NE (nullptr, lumex::test::establish_tcp (system, *node0, node1->network.endpoint ()));
	ASSERT_TIMELY_EQ (10s, node1->balance (key2.pub), node0->config.receive_minimum.number ());
}

TEST (node, merge_peers)
{
	lumex::test::system system (1);
	std::array<lumex::endpoint, 8> endpoints;
	endpoints.fill (lumex::endpoint (boost::asio::ip::address_v6::loopback (), system.get_available_port ()));
	endpoints[0] = lumex::endpoint (boost::asio::ip::address_v6::loopback (), system.get_available_port ());
	system.nodes[0]->network.merge_peers (endpoints);
	ASSERT_EQ (0, system.nodes[0]->network.size ());
}

TEST (node, search_receivable)
{
	lumex::test::system system (1);
	auto node (system.nodes[0]);
	lumex::keypair key2;
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (0)->search_receivable ());
	ASSERT_TIMELY (10s, !node->balance (key2.pub).is_zero ());
}

TEST (node, search_receivable_same)
{
	lumex::test::system system (1);
	auto node (system.nodes[0]);
	lumex::keypair key2;
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (0)->search_receivable ());
	ASSERT_TIMELY_EQ (10s, node->balance (key2.pub), 2 * node->config.receive_minimum.number ());
}

TEST (node, search_receivable_multiple)
{
	lumex::test::system system (1);
	auto node (system.nodes[0]);
	lumex::keypair key2;
	lumex::keypair key3;
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (key3.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key3.pub, node->config.receive_minimum.number ()));
	ASSERT_TIMELY (10s, !node->balance (key3.pub).is_zero ());
	ASSERT_NE (nullptr, system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, system.wallet (0)->send_action (key3.pub, key2.pub, node->config.receive_minimum.number ()));
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (0)->search_receivable ());
	ASSERT_TIMELY_EQ (10s, node->balance (key2.pub), 2 * node->config.receive_minimum.number ());
}

TEST (node, search_receivable_confirmed)
{
	lumex::test::system system;
	lumex::node_config node_config;
	node_config.peering_port = system.get_available_port ();
	node_config.backlog_scan->enable = false;
	auto node = system.add_node (node_config);
	lumex::keypair key2;
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);

	auto send1 (system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send1);
	ASSERT_TIMELY (5s, lumex::test::confirmed (*node, { send1 }));

	auto send2 (system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_TIMELY (5s, lumex::test::confirmed (*node, { send2 }));

	system.wallet (0)->remove_account (lumex::dev::genesis_key.pub);

	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (0)->search_receivable ());
	ASSERT_TIMELY (5s, !node->vote_router.active (send1->hash ()));
	ASSERT_TIMELY (5s, !node->vote_router.active (send2->hash ()));
	ASSERT_TIMELY_EQ (5s, node->balance (key2.pub), 2 * node->config.receive_minimum.number ());
}

TEST (node, search_receivable_pruned)
{
	lumex::test::system system;
	lumex::node_config node_config;
	node_config.peering_port = system.get_available_port ();
	node_config.backlog_scan->enable = false;
	auto node1 = system.add_node (node_config);
	lumex::node_flags node_flags;
	node_flags.enable_pruning = true;
	node_flags.disable_topo_index = true; // Topo index is incompatible with pruning
	lumex::node_config config;
	config.peering_port = system.get_available_port ();
	config.enable_voting = false; // Remove after allowing pruned voting
	auto node2 = system.add_node (config, node_flags);
	lumex::keypair key2;
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	auto send1 (system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key2.pub, node2->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send1);
	auto send2 (system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key2.pub, node2->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send2);

	// Confirmation
	ASSERT_TIMELY (10s, node1->active.empty () && node2->active.empty ());
	ASSERT_TIMELY (5s, node1->ledger.cemented.block_exists_or_pruned (node1->ledger.tx_begin_read (), send2->hash ()));
	ASSERT_TIMELY_EQ (5s, node2->ledger.cemented_count (), 3);
	system.wallet (0)->remove_account (lumex::dev::genesis_key.pub);

	// Pruning
	{
		auto transaction = node2->ledger.tx_begin_write ();
		ASSERT_EQ (1, node2->ledger.pruning_action (transaction, send1->hash (), 1));
	}
	ASSERT_EQ (1, node2->ledger.pruned_count ());
	ASSERT_TRUE (node2->block_or_pruned_exists (send1->hash ())); // true for pruned

	// Receive pruned block
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (1)->search_receivable ());
	ASSERT_TIMELY_EQ (10s, node2->balance (key2.pub), 2 * node2->config.receive_minimum.number ());
}

TEST (node, unlock_search)
{
	lumex::test::system system (1);
	auto node (system.nodes[0]);
	lumex::keypair key2;
	lumex::uint128_t balance (node->balance (lumex::dev::genesis_key.pub));
	system.wallet (0)->rekey ("");
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_TIMELY (10s, node->balance (lumex::dev::genesis_key.pub) != balance);
	ASSERT_TIMELY (10s, node->active.empty ());
	system.wallet (0)->insert_adhoc (key2.prv);
	{
		lumex::lock_guard<std::recursive_mutex> lock{ system.wallet (0)->store.mutex };
		system.wallet (0)->store.password.value_set (lumex::keypair ().prv);
	}
	{
		ASSERT_FALSE (system.wallet (0)->enter_password (""));
	}
	ASSERT_TIMELY (10s, !node->balance (key2.pub).is_zero ());
}

TEST (node, working)
{
	auto path (lumex::working_path ());
	ASSERT_FALSE (path.empty ());
}

TEST (node, confirm_locked)
{
	lumex::test::system system (1);
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	system.wallet (0)->enter_password ("1");
	auto block = lumex::send_block_builder ()
				 .previous (0)
				 .destination (0)
				 .balance (0)
				 .sign (lumex::keypair ().prv, 0)
				 .work (0)
				 .build ();
	system.nodes[0]->network.flood_block (block, lumex::transport::traffic_type::test);
}

TEST (node_config, random_rep)
{
	auto path (lumex::unique_path ());
	lumex::node_config config1;
	config1.peering_port = 100;
	auto rep (config1.random_representative ());
	ASSERT_NE (config1.preconfigured_representatives.end (), std::find (config1.preconfigured_representatives.begin (), config1.preconfigured_representatives.end (), rep));
}

TEST (node, expire)
{
	std::weak_ptr<lumex::node> node0;
	{
		lumex::test::system system (1);
		node0 = system.nodes[0];
		auto & node1 (*system.nodes[0]);
		system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	}
	ASSERT_TRUE (node0.expired ());
}

TEST (node, coherent_observer)
{
	lumex::test::system system (1);
	auto & node1 (*system.nodes[0]);
	node1.observers.blocks.add ([&node1] (lumex::election_status const & status_a, std::vector<lumex::vote_with_weight_info> const &, lumex::account const &, lumex::uint128_t const &, bool, bool) {
		ASSERT_TRUE (node1.ledger.any.block_exists (node1.ledger.tx_begin_read (), status_a.winner->hash ()));
	});
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	lumex::keypair key;
	system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key.pub, 1);
}

// FIXME: This test is racy, there is no guarantee that the election won't be confirmed until all forks are fully processed
/**
 * Test that two forking blocks processed in quick succession result in a
 * single election containing both blocks.
 *
 * Setup:
 * - Single node with genesis key in wallet (has voting weight)
 * - send1 and send2 (forks) are both processed via process_active
 *
 * Both blocks enter the same election. Genesis votes for send1 (the block
 * it saw first) and send1 wins the election.
 */
TEST (node, fork_publish)
{
	lumex::test::system system (1);
	auto & node1 (*system.nodes[0]);
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	lumex::keypair key1;
	lumex::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (lumex::dev::constants.genesis_amount - 100)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send1);
	lumex::keypair key2;
	auto send2 = builder.make_block ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (lumex::dev::constants.genesis_amount - 100)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send2);
	node1.process_active (send1);
	node1.process_active (send2);
	ASSERT_TIMELY (5s, node1.active.active (*send1) && node1.active.active (*send2));
	auto election (node1.active.election (send1->qualified_root ()));
	ASSERT_NE (nullptr, election);
	// Wait until the genesis rep activated & makes vote
	ASSERT_TIMELY_EQ (1s, election->votes ().size (), 2);
	auto votes1 (election->votes ());
	auto existing1 (votes1.find (lumex::dev::genesis_key.pub));
	ASSERT_NE (votes1.end (), existing1);
	ASSERT_EQ (send1->hash (), existing1->second.hash);
	auto winner (*election->tally ().begin ());
	ASSERT_EQ (*send1, *winner.second);
	ASSERT_EQ (lumex::dev::constants.genesis_amount - 100, winner.first);
}

// In test case there used to be a race condition, it was worked around in:.
// https://github.com/lumexcurrency/lumex-node/pull/4091
// The election and the processing of block send2 happen in parallel.
// Usually the election happens first and the send2 block is added to the election.
// However, if the send2 block is processed before the election is started then
// there is a race somewhere and the election might not notice the send2 block.
// The test case can be made to pass by ensuring the election is started before the send2 is processed.
// However, is this a problem with the test case or this is a problem with the node handling of forks?
// FIXME: Investigate, should work without the workaround
/**
 * Test that a fork block joins an existing election rather than being rejected.
 *
 * Setup:
 * - Single node without voting weight configured
 * - send1 is processed, creating an active election
 * - send2 (a fork of send1) is then processed via process_local
 *
 * The fork is detected (process_local returns fork status) and send2 is added
 * to the existing election. Both blocks are present in the election, with
 * send1 as the current winner since it was processed first.
 */
TEST (node, fork_publish_inactive)
{
	lumex::test::system system (1);
	auto & node = *system.nodes[0];
	lumex::keypair key1;
	lumex::keypair key2;

	lumex::send_block_builder builder;

	auto send1 = builder.make_block ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (lumex::dev::constants.genesis_amount - 100)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();

	auto send2 = builder.make_block ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (lumex::dev::constants.genesis_amount - 100)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (send1->block_work ())
				 .build ();

	node.process_active (send1);
	ASSERT_TIMELY (5s, node.block (send1->hash ()));

	std::shared_ptr<lumex::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (send1->qualified_root ()));

	ASSERT_EQ (lumex::block_status::fork, node.process_local (send2).value ());

	ASSERT_TIMELY_EQ (5s, election->blocks ().size (), 2);

	auto find_block = [&election] (lumex::block_hash hash_a) -> bool {
		auto blocks = election->blocks ();
		return blocks.end () != blocks.find (hash_a);
	};
	ASSERT_TRUE (find_block (send1->hash ()));
	ASSERT_TRUE (find_block (send2->hash ()));

	ASSERT_EQ (election->winner ()->hash (), send1->hash ());
	ASSERT_NE (election->winner ()->hash (), send2->hash ());
}

/**
 * Test that nodes keep their original block when a fork arrives later.
 *
 * Setup:
 * - Both nodes receive and process send1 first, starting elections
 * - Both nodes then receive send2 (a fork of send1)
 * - Genesis key is inserted into node1, giving it voting weight
 *
 * Node1 votes for send1 (the block in its ledger), confirming it across the
 * network. Since both nodes already have send1, no block replacement occurs.
 */
TEST (node, fork_keep)
{
	lumex::test::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	ASSERT_EQ (1, node1.network.size ());
	lumex::keypair key1;
	lumex::keypair key2;
	lumex::send_block_builder builder;
	// send1 and send2 fork to different accounts
	auto send1 = builder.make_block ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (lumex::dev::constants.genesis_amount - 100)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (lumex::dev::constants.genesis_amount - 100)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();
	node1.process_active (send1);
	node2.process_active (builder.make_block ().from (*send1).build ());
	ASSERT_TIMELY_EQ (5s, 1, node1.active.size ());
	ASSERT_TIMELY_EQ (5s, 1, node2.active.size ());
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	// Fill node with forked blocks
	node1.process_active (send2);
	ASSERT_TIMELY (5s, node1.active.active (*send2));
	node2.process_active (builder.make_block ().from (*send2).build ());
	ASSERT_TIMELY (5s, node2.active.active (*send2));
	auto election1 (node2.active.election (lumex::qualified_root (lumex::dev::genesis->hash (), lumex::dev::genesis->hash ())));
	ASSERT_NE (nullptr, election1);
	ASSERT_EQ (1, election1->votes ().size ());
	ASSERT_TRUE (node1.block_or_pruned_exists (send1->hash ()));
	ASSERT_TRUE (node2.block_or_pruned_exists (send1->hash ()));
	// Wait until the genesis rep makes a vote
	ASSERT_TIMELY (1.5min, election1->votes ().size () != 1);
	auto transaction0 (node1.ledger.tx_begin_read ());
	auto transaction1 (node2.ledger.tx_begin_read ());
	// The vote should be in agreement with what we already have.
	auto winner (*election1->tally ().begin ());
	ASSERT_EQ (*send1, *winner.second);
	ASSERT_EQ (lumex::dev::constants.genesis_amount - 100, winner.first);
	ASSERT_TRUE (node1.ledger.any.block_exists (transaction0, send1->hash ()));
	ASSERT_TRUE (node2.ledger.any.block_exists (transaction1, send1->hash ()));
}

// FIXME: This test is racy, there is no guarantee that the election won't be confirmed until all forks are fully processed
/**
 * Test basic fork resolution between two nodes that each start with a different fork.
 *
 * Setup:
 * - Node1 receives send1 first, node2 receives send2 first (both are forks)
 * - Both nodes start elections for their respective blocks
 * - Nodes then exchange forks so both elections contain both blocks
 * - Node1 votes with genesis key
 *
 * Send1 wins the election and node2 replaces send2 with send1 in its ledger.
 */
TEST (node, fork_flip)
{
	lumex::test::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	ASSERT_EQ (1, node1.network.size ());
	lumex::keypair key1;
	lumex::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (lumex::dev::constants.genesis_amount - 100)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();
	lumex::messages::publish publish1{ lumex::dev::network_params.network, send1 };
	lumex::keypair key2;
	auto send2 = builder.make_block ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (lumex::dev::constants.genesis_amount - 100)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();
	lumex::messages::publish publish2{ lumex::dev::network_params.network, send2 };
	node1.inbound (publish1, lumex::test::fake_channel (node1));
	node2.inbound (publish2, lumex::test::fake_channel (node2));
	ASSERT_TIMELY_EQ (5s, 1, node1.active.size ());
	ASSERT_TIMELY_EQ (5s, 1, node2.active.size ());
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	// Fill nodes with forked blocks
	node1.inbound (publish2, lumex::test::fake_channel (node1));
	ASSERT_TIMELY (5s, node1.active.active (*send2));
	node2.inbound (publish1, lumex::test::fake_channel (node2));
	ASSERT_TIMELY (5s, node2.active.active (*send1));
	auto election1 (node2.active.election (lumex::qualified_root (lumex::dev::genesis->hash (), lumex::dev::genesis->hash ())));
	ASSERT_NE (nullptr, election1);
	ASSERT_EQ (1, election1->votes ().size ());
	ASSERT_NE (nullptr, node1.block (publish1.block->hash ()));
	ASSERT_NE (nullptr, node2.block (publish2.block->hash ()));
	ASSERT_TIMELY (10s, node2.block_or_pruned_exists (publish1.block->hash ()));
	auto winner (*election1->tally ().begin ());
	ASSERT_EQ (*publish1.block, *winner.second);
	ASSERT_EQ (lumex::dev::constants.genesis_amount - 100, winner.first);
	ASSERT_TRUE (node1.block_or_pruned_exists (publish1.block->hash ()));
	ASSERT_TRUE (node2.block_or_pruned_exists (publish1.block->hash ()));
	ASSERT_FALSE (node2.block_or_pruned_exists (publish2.block->hash ()));
}

/**
 * Test that fork resolution correctly rolls back multiple dependent blocks.
 *
 * Setup:
 * - Node1 has send1 in its ledger
 * - Node2 has send2 (a fork of send1) plus send3 (built on top of send2)
 * - Node1 votes with genesis key and confirms send1
 *
 * When the fork is resolved, node2 must roll back both send2 and its
 * dependent block send3, then accept send1 as the confirmed winner.
 */
TEST (node, fork_multi_flip)
{
	auto type = lumex::transport::transport_type::tcp;
	lumex::test::system system;
	lumex::node_flags node_flags;
	lumex::node_config node_config;
	node_config.peering_port = system.get_available_port ();
	node_config.backlog_scan->enable = false;
	auto & node1 (*system.add_node (node_config, node_flags, type));
	node_config.peering_port = system.get_available_port ();
	node_config.bootstrap->account_sets.cooldown = 100ms; // Reduce cooldown to speed up fork resolution
	auto & node2 (*system.add_node (node_config, node_flags, type));
	ASSERT_EQ (1, node1.network.size ());
	lumex::keypair key1;
	lumex::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (lumex::dev::constants.genesis_amount - 100)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();
	lumex::keypair key2;
	auto send2 = builder.make_block ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (lumex::dev::constants.genesis_amount - 100)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();
	auto send3 = builder.make_block ()
				 .previous (send2->hash ())
				 .destination (key2.pub)
				 .balance (lumex::dev::constants.genesis_amount - 100)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (send2->hash ()))
				 .build ();
	ASSERT_EQ (lumex::block_status::progress, node1.ledger.process (node1.ledger.tx_begin_write (), send1));
	// Node2 has two blocks that will be rolled back by node1's vote
	ASSERT_EQ (lumex::block_status::progress, node2.ledger.process (node2.ledger.tx_begin_write (), send2));
	ASSERT_EQ (lumex::block_status::progress, node2.ledger.process (node2.ledger.tx_begin_write (), send3));
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv); // Insert voting key in to node1

	auto election = lumex::test::start_election (system, node2, send2->hash ());
	ASSERT_NE (nullptr, election);
	ASSERT_TIMELY (10s, election->contains (send1->hash ()));
	lumex::test::confirm (node1.ledger, send1);
	ASSERT_TIMELY (10s, node2.block_or_pruned_exists (send1->hash ()));
	ASSERT_TRUE (lumex::test::block_or_pruned_none_exists (node2, { send2, send3 }));
	auto winner = *election->tally ().begin ();
	ASSERT_EQ (*send1, *winner.second);
	ASSERT_EQ (lumex::dev::constants.genesis_amount - 100, winner.first);
}

/**
 * Test that bootstrap can resolve forks by replacing an unconfirmed block
 * with a confirmed one from a peer. This simulates a scenario where a fork
 * wasn't resolved before the node previously shut down - blocks that are no
 * longer actively being voted on should still be evictable through bootstrapping.
 *
 * Setup:
 * - Node1 (with genesis voting weight) has send1 confirmed in its ledger
 * - Node2 (disconnected) has send2 (a fork of send1) unconfirmed in its ledger
 *
 * When the nodes connect, node2 bootstraps from node1 and discovers the
 * confirmed send1. Node2 replaces send2 with send1, resolving the fork.
 */
TEST (node, fork_bootstrap_flip)
{
	lumex::test::system system;
	lumex::node_config config1;
	config1.peering_port = system.get_available_port ();
	config1.backlog_scan->enable = false;
	lumex::node_flags node_flags;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_lazy_bootstrap = true;
	auto & node1 = *system.add_node (config1, node_flags);
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	lumex::node_config config2;
	config2.peering_port = system.get_available_port ();
	config2.bootstrap->account_sets.cooldown = 100ms; // Reduce cooldown to speed up fork resolution
	auto & node2 = *system.make_disconnected_node (config2, node_flags);
	lumex::block_hash latest = node1.latest (lumex::dev::genesis_key.pub);
	lumex::keypair key1;
	lumex::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (latest)
				 .destination (key1.pub)
				 .balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (latest))
				 .build ();
	lumex::keypair key2;
	auto send2 = builder.make_block ()
				 .previous (latest)
				 .destination (key2.pub)
				 .balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (latest))
				 .build ();
	// Insert but don't rebroadcast, simulating settled blocks
	ASSERT_EQ (lumex::block_status::progress, node1.ledger.process (node1.ledger.tx_begin_write (), send1));
	ASSERT_EQ (lumex::block_status::progress, node2.ledger.process (node2.ledger.tx_begin_write (), send2));
	lumex::test::confirm (node1.ledger, send1);
	ASSERT_TIMELY (5s, node1.ledger.any.block_exists (node1.ledger.tx_begin_read (), send1->hash ()));
	ASSERT_TIMELY (5s, node2.ledger.any.block_exists (node2.ledger.tx_begin_read (), send2->hash ()));

	// Additionally add new peer to confirm & replace bootstrap block
	node2.network.merge_peer (node1.network.endpoint ());

	// Node2 should bootstrap from node1 and replace send2 with send1
	ASSERT_TIMELY (10s, node2.ledger.any.block_exists (node2.ledger.tx_begin_read (), send1->hash ()));
}

/**
 * Test fork handling for open blocks when there is no voting weight to reach quorum.
 *
 * Setup:
 * - All genesis balance is sent to key1, leaving no voting weight in the network
 * - open1 is created and processed first
 * - open2 (a fork of open1) is created and processed second
 *
 * Both blocks enter the same election. With no voting weight available, the
 * election cannot reach quorum and remains unconfirmed. The first block seen
 * (open1) is kept in the ledger while open2 is not.
 */
TEST (node, fork_open)
{
	lumex::test::system system (1);
	auto & node = *system.nodes[0];
	std::shared_ptr<lumex::election> election;

	// create block send1, to send all the balance from genesis to key1
	// this is done to ensure that the open block(s) cannot be voted on and confirmed
	lumex::keypair key1;
	auto send1 = lumex::send_block_builder ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (0)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();
	lumex::messages::publish publish1{ lumex::dev::network_params.network, send1 };
	auto channel1 = std::make_shared<lumex::transport::fake::channel> (node);
	node.inbound (publish1, channel1);
	ASSERT_TIMELY (5s, (election = node.active.election (publish1.block->qualified_root ())) != nullptr);
	election->force_confirm ();
	ASSERT_TIMELY (5s, node.active.empty () && node.block_confirmed (publish1.block->hash ()));

	// register key for genesis account, not sure why we do this, it seems needless,
	// since the genesis account at this stage has zero voting weight
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);

	// create the 1st open block to receive send1, which should be regarded as the winner just because it is first
	lumex::open_block_builder builder;
	auto open1 = builder.make_block ()
				 .source (publish1.block->hash ())
				 .representative (1)
				 .account (key1.pub)
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (key1.pub))
				 .build ();
	lumex::messages::publish publish2{ lumex::dev::network_params.network, open1 };
	node.inbound (publish2, channel1);
	ASSERT_TIMELY_EQ (5s, 1, node.active.size ());

	// create 2nd open block, which is a fork of open1 block
	auto open2 = builder.make_block ()
				 .source (publish1.block->hash ())
				 .representative (2)
				 .account (key1.pub)
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (key1.pub))
				 .build ();
	lumex::messages::publish publish3{ lumex::dev::network_params.network, open2 };
	node.inbound (publish3, channel1);
	ASSERT_TIMELY (5s, (election = node.active.election (publish3.block->qualified_root ())) != nullptr);

	// we expect to find 2 blocks in the election and we expect the first block to be the winner just because it was first
	ASSERT_TIMELY_EQ (5s, 2, election->blocks ().size ());
	ASSERT_EQ (publish2.block->hash (), election->winner ()->hash ());

	// wait for a second and check that the election did not get confirmed
	system.delay_ms (1000ms);
	ASSERT_FALSE (election->confirmed ());

	// check that only the first block is saved to the ledger
	ASSERT_TIMELY (5s, node.block (publish2.block->hash ()));
	ASSERT_FALSE (node.block (publish3.block->hash ()));
}

/**
 * Test that a node can "flip" from one fork to another when consensus resolves
 * to a different block than what it originally had in its ledger.
 *
 * Setup:
 * - Node1 has open1 in its ledger and starts an election
 * - Node2 is pre-initialized with open2 (a fork of open1) and starts an election
 * - Node1 votes with the genesis key (full voting weight) and confirms open1
 *
 * After open1 is confirmed on node1, node2 learns about open1 and must replace
 * open2 with the consensus winner in its ledger.
 */
TEST (node, fork_open_flip)
{
	lumex::test::system system (1);
	auto & node1 = *system.nodes[0];

	std::shared_ptr<lumex::election> election;
	lumex::keypair key1;
	lumex::keypair rep1;
	lumex::keypair rep2;

	// send 1 raw from genesis to key1 on both node1 and node2
	auto send1 = lumex::send_block_builder ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (lumex::dev::constants.genesis_amount - 1)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();
	node1.process_active (send1);

	// We should be keeping this block
	lumex::open_block_builder builder;
	auto open1 = builder.make_block ()
				 .source (send1->hash ())
				 .representative (rep1.pub)
				 .account (key1.pub)
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (key1.pub))
				 .build ();

	// create a fork of block open1, this block will lose the election
	auto open2 = builder.make_block ()
				 .source (send1->hash ())
				 .representative (rep2.pub)
				 .account (key1.pub)
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (key1.pub))
				 .build ();
	ASSERT_FALSE (*open1 == *open2);

	// give block open1 to node1, manually trigger an election for open1 and ensure it is in the ledger
	node1.process_active (open1);
	ASSERT_TIMELY (5s, node1.block (open1->hash ()) != nullptr);
	node1.scheduler.manual.push (open1);
	ASSERT_TIMELY (5s, (election = node1.active.election (open1->qualified_root ())) != nullptr);
	election->transition_active ();

	// create node2, with blocks send1 and open2 pre-initialised in the ledger,
	// so that block open1 cannot possibly get in the ledger before open2 via background sync
	system.initialization_blocks.push_back (send1);
	system.initialization_blocks.push_back (open2);
	auto & node2 = *system.add_node ();
	system.initialization_blocks.clear ();

	// ensure open2 is in node2 ledger (and therefore has sideband) and manually trigger an election for open2
	ASSERT_TIMELY (5s, node2.block (open2->hash ()) != nullptr);
	node2.scheduler.manual.push (open2);
	ASSERT_TIMELY (5s, (election = node2.active.election (open2->qualified_root ())) != nullptr);
	election->transition_active ();

	ASSERT_TIMELY_EQ (5s, 2, node1.active.size ());
	ASSERT_TIMELY_EQ (5s, 2, node2.active.size ());

	// allow node1 to vote and wait for open1 to be confirmed on node1
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	ASSERT_TIMELY (5s, node1.block_confirmed (open1->hash ()));

	// Notify both nodes of both blocks, both nodes will become aware that a fork exists
	node1.process_active (open2);
	node2.process_active (open1);

	ASSERT_TIMELY_EQ (5s, 2, election->votes ().size ()); // one more than expected due to elections having dummy votes

	// Node2 should eventually settle on open1
	ASSERT_TIMELY (10s, node2.block (open1->hash ()));
	ASSERT_TIMELY (5s, node1.block_confirmed (open1->hash ()));
	auto winner = *election->tally ().begin ();
	ASSERT_EQ (*open1, *winner.second);
	ASSERT_EQ (lumex::dev::constants.genesis_amount - 1, winner.first);

	// check the correct blocks are in the ledgers
	auto transaction1 = node1.ledger.tx_begin_read ();
	auto transaction2 = node2.ledger.tx_begin_read ();
	ASSERT_TRUE (node1.ledger.any.block_exists (transaction1, open1->hash ()));
	ASSERT_TRUE (node2.ledger.any.block_exists (transaction2, open1->hash ()));
	ASSERT_FALSE (node2.ledger.any.block_exists (transaction2, open2->hash ()));
}

/**
 * Test that a fork cannot displace an existing block when voted for by a representative
 * without sufficient weight to reach quorum.
 *
 * Setup:
 * - 3 connected nodes with key1 as a representative holding minimal voting weight
 * - send1: a valid block processed by all nodes
 * - send2: a forking block (same previous as send1) sent to a different destination
 *
 * The test sends a vote for the forking block (send2) from key1, which lacks
 * enough weight to reach quorum. It verifies that all nodes retain send1 as
 * the latest block in the ledger, demonstrating that votes without quorum cannot override consensus.
 */
TEST (node, fork_no_vote_quorum)
{
	lumex::test::system system (3);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	auto & node3 (*system.nodes[2]);
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	auto key4 = system.wallet (0)->deterministic_insert ().value ();
	system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key4, lumex::dev::constants.genesis_amount / 4);
	auto key1 = system.wallet (1)->deterministic_insert ().value ();
	system.wallet (1)->set_representative (key1);
	auto block = system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key1, node1.config.receive_minimum.number ());
	ASSERT_NE (nullptr, block);
	ASSERT_TIMELY (30s, node3.balance (key1) == node1.config.receive_minimum.number () && node2.balance (key1) == node1.config.receive_minimum.number () && node1.balance (key1) == node1.config.receive_minimum.number ());
	ASSERT_EQ (node1.config.receive_minimum.number (), node1.weight (key1));
	ASSERT_EQ (node1.config.receive_minimum.number (), node2.weight (key1));
	ASSERT_EQ (node1.config.receive_minimum.number (), node3.weight (key1));
	lumex::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (block->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance ((lumex::dev::constants.genesis_amount / 4) - (node1.config.receive_minimum.number () * 2))
				 .link (key1)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (block->hash ()))
				 .build ();
	lumex::test::process (node1, { send1 });
	lumex::test::process (node2, { send1 });
	lumex::test::process (node3, { send1 });
	auto key2 = system.wallet (2)->deterministic_insert ().value ();
	auto send2 = lumex::send_block_builder ()
				 .previous (block->hash ())
				 .destination (key2)
				 .balance ((lumex::dev::constants.genesis_amount / 4) - (node1.config.receive_minimum.number () * 2))
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (block->hash ()))
				 .build ();
	auto key3_result = system.wallet (1)->fetch_prv (key1);
	ASSERT_TRUE (key3_result);
	auto vote = std::make_shared<lumex::vote> (key1, key3_result.value (), 0, 0, std::vector<lumex::block_hash>{ send2->hash () });
	lumex::messages::confirm_ack confirm{ lumex::dev::network_params.network, vote };
	auto channel = node2.network.find_node_id (node3.get_node_id ());
	ASSERT_NE (nullptr, channel);
	channel->send (confirm, lumex::transport::traffic_type::test);
	ASSERT_TIMELY (10s, node3.stats.count (lumex::stat::type::message, lumex::stat::detail::confirm_ack, lumex::stat::dir::in) >= 3);
	ASSERT_EQ (node1.latest (lumex::dev::genesis_key.pub), send1->hash ());
	ASSERT_EQ (node2.latest (lumex::dev::genesis_key.pub), send1->hash ());
	ASSERT_EQ (node3.latest (lumex::dev::genesis_key.pub), send1->hash ());
}

// FIXME: Disabled because it sometimes takes way too long (but still eventually finishes)
/**
 * Test fork resolution when voting weight is distributed across multiple representatives
 * and the fork exists before any block is confirmed.
 *
 * Setup:
 * - 3 nodes, each with a representative key
 * - Genesis sends 1/3 of balance to key1 (node1's rep) and 1/3 to key2 (node2's rep)
 * - Voting weight is now split: genesis ~1/3, key1 ~1/3, key2 ~1/3
 * - block2 and block3 are forking rep-change blocks on the genesis account
 * - node0 and node1 receive block2, node2 receives block3
 *
 * Each node votes for the block it has. With 2/3 voting for block2 (genesis + key1)
 * and 1/3 for block3 (key2), block2 should win. The test verifies that all nodes
 * eventually reach consensus on the same block.
 */
TEST (node, DISABLED_fork_pre_confirm)
{
	lumex::test::system system (3);
	auto & node0 (*system.nodes[0]);
	auto & node1 (*system.nodes[1]);
	auto & node2 (*system.nodes[2]);
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	lumex::keypair key1;
	system.wallet (1)->insert_adhoc (key1.prv);
	system.wallet (1)->set_representative (key1.pub);
	lumex::keypair key2;
	system.wallet (2)->insert_adhoc (key2.prv);
	system.wallet (2)->set_representative (key2.pub);
	auto block0 (system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key1.pub, lumex::dev::constants.genesis_amount / 3));
	ASSERT_NE (nullptr, block0);
	ASSERT_TIMELY (30s, node0.balance (key1.pub) != 0);
	auto block1 (system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key2.pub, lumex::dev::constants.genesis_amount / 3));
	ASSERT_NE (nullptr, block1);
	ASSERT_TIMELY (30s, node0.balance (key2.pub) != 0);
	lumex::keypair key3;
	lumex::keypair key4;
	lumex::state_block_builder builder;
	auto block2 = builder.make_block ()
				  .account (lumex::dev::genesis_key.pub)
				  .previous (node0.latest (lumex::dev::genesis_key.pub))
				  .representative (key3.pub)
				  .balance (node0.balance (lumex::dev::genesis_key.pub))
				  .link (0)
				  .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				  .work (0)
				  .build ();
	auto block3 = builder.make_block ()
				  .account (lumex::dev::genesis_key.pub)
				  .previous (node0.latest (lumex::dev::genesis_key.pub))
				  .representative (key4.pub)
				  .balance (node0.balance (lumex::dev::genesis_key.pub))
				  .link (0)
				  .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				  .work (0)
				  .build ();
	node0.work_generate_blocking (*block2);
	node0.work_generate_blocking (*block3);
	node0.process_active (block2);
	node1.process_active (block2);
	node2.process_active (block3);
	auto done (false);
	// Extend deadline; we must finish within a total of 100 seconds
	system.deadline_set (70s);
	while (!done)
	{
		done |= node0.latest (lumex::dev::genesis_key.pub) == block2->hash () && node1.latest (lumex::dev::genesis_key.pub) == block2->hash () && node2.latest (lumex::dev::genesis_key.pub) == block2->hash ();
		done |= node0.latest (lumex::dev::genesis_key.pub) == block3->hash () && node1.latest (lumex::dev::genesis_key.pub) == block3->hash () && node2.latest (lumex::dev::genesis_key.pub) == block3->hash ();
		ASSERT_NO_ERROR (system.poll ());
	}
}

// FIXME: Disabled because it is hanging intermittently
/**
 * Test fork resolution between nodes that independently processed different
 * blocks before discovering each other.
 *
 * Setup:
 * - Two separate test systems with one node each (isolated networks)
 * - Node1 has genesis key (voting weight), node2 does not
 * - A TCP channel is established and node2 learns node1 is a representative
 * - send3 is processed on both nodes (shared base block)
 * - send1 is written directly to node1's ledger
 * - send2 (fork of send1) is written directly to node2's ledger
 * - Both forks are then activated on both nodes via process_active
 *
 * The direct ledger writes simulate "stale" blocks that existed before the
 * nodes connected. When both forks are activated, node1's vote for send1
 * should cause node2 to accept send1.
 */
TEST (node, DISABLED_fork_stale)
{
	lumex::test::system system1 (1);
	system1.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	lumex::test::system system2 (1);
	auto & node1 (*system1.nodes[0]);
	auto & node2 (*system2.nodes[0]);

	auto channel = lumex::test::establish_tcp (system1, node2, node1.network.endpoint ());
	auto vote = std::make_shared<lumex::vote> (lumex::dev::genesis_key.pub, lumex::dev::genesis_key.prv, 0, 0, std::vector<lumex::block_hash> ());
	ASSERT_TRUE (node2.rep_crawler.process (vote, channel));
	lumex::keypair key1;
	lumex::keypair key2;
	lumex::state_block_builder builder;
	auto send3 = builder.make_block ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (lumex::dev::genesis->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - lumex::lumex_ratio)
				 .link (key1.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send3);
	node1.process_active (send3);
	system2.deadline_set (10s);
	while (node2.block (send3->hash ()) == nullptr)
	{
		system1.poll ();
		ASSERT_NO_ERROR (system2.poll ());
	}
	auto send1 = builder.make_block ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (send3->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - 2 * lumex::lumex_ratio)
				 .link (key1.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send1);
	auto send2 = builder.make_block ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (send3->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - 2 * lumex::lumex_ratio)
				 .link (key2.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send2);
	{
		auto transaction1 = node1.ledger.tx_begin_write ();
		ASSERT_EQ (lumex::block_status::progress, node1.ledger.process (transaction1, send1));
		auto transaction2 = node2.ledger.tx_begin_write ();
		ASSERT_EQ (lumex::block_status::progress, node2.ledger.process (transaction2, send2));
	}
	node1.process_active (send1);
	node1.process_active (send2);
	node2.process_active (send1);
	node2.process_active (send2);
	while (node2.block (send1->hash ()) == nullptr)
	{
		system1.poll ();
		ASSERT_NO_ERROR (system2.poll ());
	}
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/lumexcurrency/lumex-node/pull/3512
// Issue for investigating it: https://github.com/lumexcurrency/lumex-node/issues/3516
TEST (node, DISABLED_broadcast_elected)
{
	auto type = lumex::transport::transport_type::tcp;
	lumex::node_flags node_flags;
	lumex::test::system system;
	lumex::node_config node_config;
	node_config.peering_port = system.get_available_port ();
	node_config.backlog_scan->enable = false;
	auto node0 = system.add_node (node_config, node_flags, type);
	node_config.peering_port = system.get_available_port ();
	auto node1 = system.add_node (node_config, node_flags, type);
	node_config.peering_port = system.get_available_port ();
	auto node2 = system.add_node (node_config, node_flags, type);
	lumex::keypair rep_big;
	lumex::keypair rep_small;
	lumex::keypair rep_other;
	lumex::block_builder builder;
	{
		auto transaction0 = node0->ledger.tx_begin_write ();
		auto transaction1 = node1->ledger.tx_begin_write ();
		auto transaction2 = node2->ledger.tx_begin_write ();
		auto fund_big = builder.send ()
						.previous (lumex::dev::genesis->hash ())
						.destination (rep_big.pub)
						.balance (lumex::Klumex_ratio * 5)
						.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
						.work (*system.work.generate (lumex::dev::genesis->hash ()))
						.build ();
		auto open_big = builder.open ()
						.source (fund_big->hash ())
						.representative (rep_big.pub)
						.account (rep_big.pub)
						.sign (rep_big.prv, rep_big.pub)
						.work (*system.work.generate (rep_big.pub))
						.build ();
		auto fund_small = builder.send ()
						  .previous (fund_big->hash ())
						  .destination (rep_small.pub)
						  .balance (lumex::Klumex_ratio * 2)
						  .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
						  .work (*system.work.generate (fund_big->hash ()))
						  .build ();
		auto open_small = builder.open ()
						  .source (fund_small->hash ())
						  .representative (rep_small.pub)
						  .account (rep_small.pub)
						  .sign (rep_small.prv, rep_small.pub)
						  .work (*system.work.generate (rep_small.pub))
						  .build ();
		auto fund_other = builder.send ()
						  .previous (fund_small->hash ())
						  .destination (rep_other.pub)
						  .balance (lumex::Klumex_ratio)
						  .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
						  .work (*system.work.generate (fund_small->hash ()))
						  .build ();
		auto open_other = builder.open ()
						  .source (fund_other->hash ())
						  .representative (rep_other.pub)
						  .account (rep_other.pub)
						  .sign (rep_other.prv, rep_other.pub)
						  .work (*system.work.generate (rep_other.pub))
						  .build ();
		ASSERT_EQ (lumex::block_status::progress, node0->ledger.process (transaction0, fund_big));
		ASSERT_EQ (lumex::block_status::progress, node1->ledger.process (transaction1, fund_big));
		ASSERT_EQ (lumex::block_status::progress, node2->ledger.process (transaction2, fund_big));
		ASSERT_EQ (lumex::block_status::progress, node0->ledger.process (transaction0, open_big));
		ASSERT_EQ (lumex::block_status::progress, node1->ledger.process (transaction1, open_big));
		ASSERT_EQ (lumex::block_status::progress, node2->ledger.process (transaction2, open_big));
		ASSERT_EQ (lumex::block_status::progress, node0->ledger.process (transaction0, fund_small));
		ASSERT_EQ (lumex::block_status::progress, node1->ledger.process (transaction1, fund_small));
		ASSERT_EQ (lumex::block_status::progress, node2->ledger.process (transaction2, fund_small));
		ASSERT_EQ (lumex::block_status::progress, node0->ledger.process (transaction0, open_small));
		ASSERT_EQ (lumex::block_status::progress, node1->ledger.process (transaction1, open_small));
		ASSERT_EQ (lumex::block_status::progress, node2->ledger.process (transaction2, open_small));
		ASSERT_EQ (lumex::block_status::progress, node0->ledger.process (transaction0, fund_other));
		ASSERT_EQ (lumex::block_status::progress, node1->ledger.process (transaction1, fund_other));
		ASSERT_EQ (lumex::block_status::progress, node2->ledger.process (transaction2, fund_other));
		ASSERT_EQ (lumex::block_status::progress, node0->ledger.process (transaction0, open_other));
		ASSERT_EQ (lumex::block_status::progress, node1->ledger.process (transaction1, open_other));
		ASSERT_EQ (lumex::block_status::progress, node2->ledger.process (transaction2, open_other));
	}
	// Confirm blocks to allow voting
	for (auto & node : system.nodes)
	{
		auto block (node->block (node->latest (lumex::dev::genesis_key.pub)));
		ASSERT_NE (nullptr, block);
		node->start_election (block);
		auto election (node->active.election (block->qualified_root ()));
		ASSERT_NE (nullptr, election);
		election->force_confirm ();
		ASSERT_TIMELY_EQ (5s, 4, node->ledger.cemented_count ())
	}

	system.wallet (0)->insert_adhoc (rep_big.prv);
	system.wallet (1)->insert_adhoc (rep_small.prv);
	system.wallet (2)->insert_adhoc (rep_other.prv);
	auto fork0 = builder.send ()
				 .previous (node2->latest (lumex::dev::genesis_key.pub))
				 .destination (rep_small.pub)
				 .balance (0)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*node0->work_generate_blocking (node2->latest (lumex::dev::genesis_key.pub)))
				 .build ();
	// A copy is necessary to avoid data races during ledger processing, which sets the sideband
	auto fork0_copy (std::make_shared<lumex::send_block> (*fork0));
	node0->process_active (fork0);
	node1->process_active (fork0_copy);
	auto fork1 = builder.send ()
				 .previous (node2->latest (lumex::dev::genesis_key.pub))
				 .destination (rep_big.pub)
				 .balance (0)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*node0->work_generate_blocking (node2->latest (lumex::dev::genesis_key.pub)))
				 .build ();
	system.wallet (2)->insert_adhoc (rep_small.prv);
	node2->process_active (fork1);
	ASSERT_TIMELY (10s, node0->block_or_pruned_exists (fork0->hash ()) && node1->block_or_pruned_exists (fork0->hash ()));
	system.deadline_set (50s);
	while (!node2->block_or_pruned_exists (fork0->hash ()))
	{
		auto ec = system.poll ();
		ASSERT_TRUE (node0->block_or_pruned_exists (fork0->hash ()));
		ASSERT_TRUE (node1->block_or_pruned_exists (fork0->hash ()));
		ASSERT_NO_ERROR (ec);
	}
	ASSERT_TIMELY (5s, node1->stats.count (lumex::stat::type::confirmation_observer, lumex::stat::detail::inactive_conf_height, lumex::stat::dir::out) != 0);
}

TEST (node, rep_self_vote)
{
	lumex::test::system system;

	lumex::node_flags node_flags;
	node_flags.disable_request_loop = true; // Prevent automatic election cleanup
	lumex::node_config node_config = system.default_config ();
	node_config.online_weight_minimum = std::numeric_limits<lumex::uint128_t>::max ();
	// Disable automatic election activation
	node_config.backlog_scan->enable = false;
	node_config.priority_scheduler->enable = false;
	node_config.hinted_scheduler->enable = false;
	node_config.optimistic_scheduler->enable = false;
	auto node0 = system.add_node (node_config, node_flags);

	lumex::keypair rep_big;
	lumex::block_builder builder;
	auto fund_big = builder.send ()
					.previous (lumex::dev::genesis->hash ())
					.destination (rep_big.pub)
					.balance (lumex::uint128_t{ "0xb0000000000000000000000000000000" })
					.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					.work (*system.work.generate (lumex::dev::genesis->hash ()))
					.build ();
	auto open_big = builder.open ()
					.source (fund_big->hash ())
					.representative (rep_big.pub)
					.account (rep_big.pub)
					.sign (rep_big.prv, rep_big.pub)
					.work (*system.work.generate (rep_big.pub))
					.build ();
	ASSERT_EQ (lumex::block_status::progress, node0->process (fund_big));
	ASSERT_EQ (lumex::block_status::progress, node0->process (open_big));

	// Confirm both blocks, allowing voting on the upcoming block
	node0->start_election (node0->block (open_big->hash ()));

	std::shared_ptr<lumex::election> election;
	ASSERT_TIMELY (5s, election = node0->active.election (open_big->qualified_root ()));
	election->force_confirm ();

	// Insert representatives into the node to allow voting
	system.wallet (0)->insert_adhoc (rep_big.prv);
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	ASSERT_EQ (system.wallet (0)->wallets.reps ().voting, 2);

	auto block0 = builder.send ()
				  .previous (fund_big->hash ())
				  .destination (rep_big.pub)
				  .balance (lumex::uint128_t ("0x60000000000000000000000000000000"))
				  .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				  .work (*system.work.generate (fund_big->hash ()))
				  .build ();
	ASSERT_EQ (lumex::block_status::progress, node0->process (block0));

	auto election1 = lumex::test::start_election (system, *node0, block0->hash ());
	ASSERT_NE (nullptr, election1);

	// Wait until representatives are activated & make vote
	ASSERT_TIMELY_EQ (1s, election1->votes ().size (), 3);

	// Election should receive votes from representatives hosted on the same node
	auto rep_votes (election1->votes ());
	ASSERT_NE (rep_votes.end (), rep_votes.find (lumex::dev::genesis_key.pub));
	ASSERT_NE (rep_votes.end (), rep_votes.find (rep_big.pub));
}

// Bootstrapping shouldn't republish the blocks to the network.
TEST (node, DISABLED_bootstrap_no_publish)
{
	lumex::test::system system0 (1);
	lumex::test::system system1 (1);
	auto node0 (system0.nodes[0]);
	auto node1 (system1.nodes[0]);
	lumex::keypair key0;
	// node0 knows about send0 but node1 doesn't.
	lumex::block_builder builder;
	auto send0 = builder
				 .send ()
				 .previous (node0->latest (lumex::dev::genesis_key.pub))
				 .destination (key0.pub)
				 .balance (500)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	{
		auto transaction = node0->ledger.tx_begin_write ();
		ASSERT_EQ (lumex::block_status::progress, node0->ledger.process (transaction, send0));
	}
	ASSERT_TRUE (node1->active.empty ());
	system1.deadline_set (10s);
	while (node1->block (send0->hash ()) == nullptr)
	{
		// Poll until the TCP connection is torn down and in_progress goes false
		system0.poll ();
		auto ec = system1.poll ();
		// There should never be an active transaction because the only activity is bootstrapping 1 block which shouldn't be publishing.
		ASSERT_TRUE (node1->active.empty ());
		ASSERT_NO_ERROR (ec);
	}
}

// Bootstrapping a forked open block should succeed.
TEST (node, bootstrap_fork_open)
{
	lumex::test::system system;
	lumex::node_config node_config;
	node_config.peering_port = system.get_available_port ();
	node_config.bootstrap->account_sets.cooldown = 100ms; // Reduce cooldown to speed up fork resolution
	node_config.bootstrap->frontier_scan.head_parallelism = 3; // Make sure we can process the full account number range
	node_config.bootstrap->frontier_rate_limit = 0; // Disable rate limiting to speed up the scan
	// Disable automatic election activation
	node_config.backlog_scan->enable = false;
	node_config.priority_scheduler->enable = false;
	node_config.hinted_scheduler->enable = false;
	node_config.optimistic_scheduler->enable = false;
	auto node0 = system.add_node (node_config);
	node_config.peering_port = system.get_available_port ();
	auto node1 = system.add_node (node_config);
	lumex::keypair key0;
	lumex::block_builder builder;
	auto send0 = builder.send ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key0.pub)
				 .balance (lumex::dev::constants.genesis_amount - 500)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();
	auto open0 = builder.open ()
				 .source (send0->hash ())
				 .representative (1)
				 .account (key0.pub)
				 .sign (key0.prv, key0.pub)
				 .work (*system.work.generate (key0.pub))
				 .build ();
	auto open1 = builder.open ()
				 .source (send0->hash ())
				 .representative (2)
				 .account (key0.pub)
				 .sign (key0.prv, key0.pub)
				 .work (*system.work.generate (key0.pub))
				 .build ();

	// Both know about send0
	ASSERT_EQ (lumex::block_status::progress, node0->process (send0));
	ASSERT_EQ (lumex::block_status::progress, node1->process (send0));

	// Confirm send0 to allow starting and voting on the following blocks
	lumex::test::confirm (*node0, { send0 });
	lumex::test::confirm (*node1, { send0 });
	ASSERT_TIMELY (5s, node0->block_confirmed (send0->hash ()));
	ASSERT_TIMELY (5s, node1->block_confirmed (send0->hash ()));

	// They disagree about open0/open1
	ASSERT_EQ (lumex::block_status::progress, node0->process (open0));
	node0->cementing_set.add (open0->hash ());
	ASSERT_TIMELY (5s, node0->block_confirmed (open0->hash ()));

	ASSERT_EQ (lumex::block_status::progress, node1->process (open1));
	ASSERT_TRUE (node1->block_or_pruned_exists (open1->hash ()));
	node1->start_election (open1); // Start election for open block which is necessary to resolve the fork
	ASSERT_TIMELY (5s, node1->active.active (*open1));

	// Allow node0 to vote on its fork
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);

	ASSERT_TIMELY (10s, !node1->block_or_pruned_exists (open1->hash ()) && node1->block_or_pruned_exists (open0->hash ()));
}

// Unconfirmed blocks from bootstrap should be confirmed
TEST (node, bootstrap_confirm_frontiers)
{
	lumex::test::system system;
	auto node0 = system.add_node ();
	auto node1 = system.add_node ();
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	lumex::keypair key0;

	// create block to send 500 raw from genesis to key0 and save into node0 ledger without immediately triggering an election
	auto send0 = lumex::send_block_builder ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key0.pub)
				 .balance (lumex::dev::constants.genesis_amount - 500)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*node0->work_generate_blocking (lumex::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (lumex::block_status::progress, node0->process (send0));
	ASSERT_TIMELY (10s, node1->block_confirmed (send0->hash ()));
}

// Test that if we create a block that isn't confirmed, the bootstrapping processes sync the missing block.
TEST (node, unconfirmed_send)
{
	lumex::test::system system{};

	auto & node1 = *system.add_node ();
	auto wallet1 = system.wallet (0);
	wallet1->insert_adhoc (lumex::dev::genesis_key.prv);

	lumex::keypair key2{};
	auto & node2 = *system.add_node ();
	auto wallet2 = system.wallet (1);
	wallet2->insert_adhoc (key2.prv);

	// firstly, send two units from node1 to node2 and expect that both nodes see the block as confirmed
	// (node1 will start an election for it, vote on it and node2 gets synced up)
	auto send1 = wallet1->send_action (lumex::dev::genesis_key.pub, key2.pub, 2 * lumex::lumex_ratio);
	ASSERT_TIMELY (5s, node1.block_confirmed (send1->hash ()));
	ASSERT_TIMELY (5s, node2.block_confirmed (send1->hash ()));

	// wait until receive1 (auto-receive created by wallet) is cemented
	ASSERT_TIMELY_EQ (5s, node2.ledger.cemented.account_height (node2.ledger.tx_begin_read (), key2.pub), 1);
	ASSERT_EQ (node2.balance (key2.pub), 2 * lumex::lumex_ratio);
	auto recv1 = node2.ledger.find_receive_block_by_send_hash (node2.ledger.tx_begin_read (), key2.pub, send1->hash ());

	// create send2 to send from node2 to node1 and save it to node2's ledger without triggering an election (node1 does not hear about it)
	auto send2 = lumex::state_block_builder{}
				 .make_block ()
				 .account (key2.pub)
				 .previous (recv1->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::lumex_ratio)
				 .link (lumex::dev::genesis_key.pub)
				 .sign (key2.prv, key2.pub)
				 .work (*system.work.generate (recv1->hash ()))
				 .build ();
	ASSERT_EQ (lumex::block_status::progress, node2.process (send2));

	auto send3 = wallet2->send_action (key2.pub, lumex::dev::genesis_key.pub, lumex::lumex_ratio);
	ASSERT_TIMELY (5s, node2.block_confirmed (send2->hash ()));
	ASSERT_TIMELY (5s, node1.block_confirmed (send2->hash ()));
	ASSERT_TIMELY (5s, node2.block_confirmed (send3->hash ()));
	ASSERT_TIMELY (5s, node1.block_confirmed (send3->hash ()));
	ASSERT_TIMELY_EQ (5s, node2.ledger.cemented_count (), 7);
	ASSERT_TIMELY_EQ (5s, node1.balance (lumex::dev::genesis_key.pub), lumex::dev::constants.genesis_amount);
}

// Test that nodes can disable representative voting
TEST (node, no_voting)
{
	lumex::test::system system (1);
	auto & node0 (*system.nodes[0]);
	lumex::node_config node_config;
	node_config.peering_port = system.get_available_port ();
	node_config.enable_voting = false;
	system.add_node (node_config);

	auto wallet0 (system.wallet (0));
	auto wallet1 (system.wallet (1));
	// Node1 has a rep
	wallet1->insert_adhoc (lumex::dev::genesis_key.prv);
	lumex::keypair key1;
	wallet1->insert_adhoc (key1.prv);
	// Broadcast a confirm so others should know this is a rep node
	wallet1->send_action (lumex::dev::genesis_key.pub, key1.pub, lumex::lumex_ratio);
	ASSERT_TIMELY (10s, node0.active.empty ());
	ASSERT_EQ (0, node0.stats.count (lumex::stat::type::message, lumex::stat::detail::confirm_ack, lumex::stat::dir::in));
}

TEST (node, send_callback)
{
	lumex::test::system system (1);
	auto & node0 (*system.nodes[0]);
	lumex::keypair key2;
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	node0.config.callback_address = "localhost";
	node0.config.callback_port = 8010;
	node0.config.callback_target = "/";
	ASSERT_NE (nullptr, system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key2.pub, node0.config.receive_minimum.number ()));
	ASSERT_TIMELY (10s, node0.balance (key2.pub).is_zero ());
	ASSERT_EQ (std::numeric_limits<lumex::uint128_t>::max () - node0.config.receive_minimum.number (), node0.balance (lumex::dev::genesis_key.pub));
}

TEST (node, balance_observer)
{
	lumex::test::system system (1);
	auto & node1 (*system.nodes[0]);
	std::atomic<int> balances (0);
	lumex::keypair key;
	node1.observers.account_balance.add ([&key, &balances] (lumex::account const & account_a, bool is_pending) {
		if (key.pub == account_a && is_pending)
		{
			balances++;
		}
		else if (lumex::dev::genesis_key.pub == account_a && !is_pending)
		{
			balances++;
		}
	});
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key.pub, 1);
	system.deadline_set (10s);
	auto done (false);
	while (!done)
	{
		auto ec = system.poll ();
		done = balances.load () == 2;
		ASSERT_NO_ERROR (ec);
	}
}

TEST (node, block_confirm)
{
	auto type = lumex::transport::transport_type::tcp;
	lumex::node_flags node_flags;
	lumex::test::system system (2, type, node_flags);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	lumex::keypair key;
	lumex::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (lumex::dev::genesis->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				 .link (key.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (lumex::dev::genesis->hash ()))
				 .build ();
	// A copy is necessary to avoid data races during ledger processing, which sets the sideband
	auto send1_copy = builder.make_block ()
					  .from (*send1)
					  .build ();
	auto hash1 = send1->hash ();
	auto hash2 = send1_copy->hash ();
	node1.block_processor.add (send1, lumex::block_source::test);
	node2.block_processor.add (send1_copy, lumex::block_source::test);
	ASSERT_TIMELY (5s, node1.block_or_pruned_exists (send1->hash ()) && node2.block_or_pruned_exists (send1_copy->hash ()));
	ASSERT_TRUE (node1.block_or_pruned_exists (send1->hash ()));
	ASSERT_TRUE (node2.block_or_pruned_exists (send1_copy->hash ()));
	// Confirm send1 on node2 so it can vote for send2
	node2.start_election (send1_copy);
	std::shared_ptr<lumex::election> election;
	ASSERT_TIMELY (5s, election = node2.active.election (send1_copy->qualified_root ()));
	// Make node2 genesis representative so it can vote
	system.wallet (1)->insert_adhoc (lumex::dev::genesis_key.prv);
	ASSERT_TIMELY_EQ (10s, node1.active.recently_cemented.size (), 1);
}

TEST (node, confirm_quorum)
{
	lumex::test::system system (1);
	auto & node1 = *system.nodes[0];
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	// Put greater than node.delta () in pending so quorum can't be reached
	lumex::amount new_balance = node1.online_reps.delta () - lumex::Klumex_ratio;
	auto send1 = lumex::state_block_builder ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (lumex::dev::genesis->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (new_balance)
				 .link (lumex::dev::genesis_key.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (lumex::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (lumex::block_status::progress, node1.process (send1));
	system.wallet (0)->send_action (lumex::dev::genesis_key.pub, lumex::dev::genesis_key.pub, new_balance.number ());
	ASSERT_TIMELY (2s, node1.active.election (send1->qualified_root ()));
	auto election = node1.active.election (send1->qualified_root ());
	ASSERT_NE (nullptr, election);
	ASSERT_FALSE (election->confirmed ());
	ASSERT_EQ (1, election->votes ().size ());
	ASSERT_EQ (0, node1.balance (lumex::dev::genesis_key.pub));
}

// TODO: Local vote cache is no longer used when generating votes
TEST (node, DISABLED_local_votes_cache)
{
	lumex::test::system system;
	lumex::node_config node_config;
	node_config.peering_port = system.get_available_port ();
	node_config.backlog_scan->enable = false;
	node_config.receive_minimum = lumex::dev::constants.genesis_amount;
	auto & node (*system.add_node (node_config));
	lumex::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (lumex::dev::genesis->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				 .link (lumex::dev::genesis_key.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (lumex::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - 2 * lumex::Klumex_ratio)
				 .link (lumex::dev::genesis_key.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (send1->hash ()))
				 .build ();
	auto send3 = builder.make_block ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (send2->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - 3 * lumex::Klumex_ratio)
				 .link (lumex::dev::genesis_key.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (send2->hash ()))
				 .build ();
	{
		auto transaction = node.ledger.tx_begin_write ();
		ASSERT_EQ (lumex::block_status::progress, node.ledger.process (transaction, send1));
		ASSERT_EQ (lumex::block_status::progress, node.ledger.process (transaction, send2));
	}
	// Confirm blocks to allow voting
	node.start_election (send2);
	std::shared_ptr<lumex::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (send2->qualified_root ()));
	election->force_confirm ();
	ASSERT_TIMELY_EQ (3s, node.ledger.cemented_count (), 3);
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	lumex::messages::confirm_req message1{ lumex::dev::network_params.network, send1->hash (), send1->root () };
	lumex::messages::confirm_req message2{ lumex::dev::network_params.network, send2->hash (), send2->root () };
	auto channel = std::make_shared<lumex::transport::fake::channel> (node);
	node.inbound (message1, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (lumex::stat::type::vote_replier, lumex::stat::detail::reply_final), 1);
	node.inbound (message2, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (lumex::stat::type::vote_replier, lumex::stat::detail::reply_final), 2);
	for (auto i (0); i < 100; ++i)
	{
		node.inbound (message1, channel);
		node.inbound (message2, channel);
	}
	// Make sure a new vote was not generated
	ASSERT_TIMELY_EQ (3s, node.stats.count (lumex::stat::type::vote_replier, lumex::stat::detail::reply_final), 2);
	// Max cache
	{
		auto transaction = node.ledger.tx_begin_write ();
		ASSERT_EQ (lumex::block_status::progress, node.ledger.process (transaction, send3));
	}
	lumex::messages::confirm_req message3{ lumex::dev::network_params.network, send3->hash (), send3->root () };
	node.inbound (message3, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (lumex::stat::type::vote_replier, lumex::stat::detail::reply_final), 3);
	ASSERT_TIMELY (3s, !node.history.votes (send1->root (), send1->hash ()).empty ());
	ASSERT_TIMELY (3s, !node.history.votes (send2->root (), send2->hash ()).empty ());
	ASSERT_TIMELY (3s, !node.history.votes (send3->root (), send3->hash ()).empty ());
	// All requests should be served from the cache
	for (auto i (0); i < 100; ++i)
	{
		node.inbound (message3, channel);
	}
	ASSERT_TIMELY_EQ (3s, node.stats.count (lumex::stat::type::vote_replier, lumex::stat::detail::reply_final), 3);
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/lumexcurrency/lumex-node/pull/3532
// Issue for investigating it: https://github.com/lumexcurrency/lumex-node/issues/3481
// TODO: Local vote cache is no longer used when generating votes
TEST (node, DISABLED_local_votes_cache_batch)
{
	lumex::test::system system;
	lumex::node_config node_config;
	node_config.peering_port = system.get_available_port ();
	node_config.backlog_scan->enable = false;
	auto & node (*system.add_node (node_config));
	ASSERT_GE (node.network_params.voting.max_cache, 2);
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	lumex::keypair key1;
	auto send1 = lumex::state_block_builder ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (lumex::dev::genesis->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				 .link (key1.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (lumex::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (lumex::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send1));
	node.cementing_set.add (send1->hash ());
	ASSERT_TIMELY (5s, node.ledger.cemented.block_exists_or_pruned (node.ledger.tx_begin_read (), send1->hash ()));
	auto send2 = lumex::state_block_builder ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - 2 * lumex::Klumex_ratio)
				 .link (lumex::dev::genesis_key.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (send1->hash ()))
				 .build ();
	ASSERT_EQ (lumex::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), send2));
	auto receive1 = lumex::state_block_builder ()
					.account (key1.pub)
					.previous (0)
					.representative (lumex::dev::genesis_key.pub)
					.balance (lumex::Klumex_ratio)
					.link (send1->hash ())
					.sign (key1.prv, key1.pub)
					.work (*node.work_generate_blocking (key1.pub))
					.build ();
	ASSERT_EQ (lumex::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), receive1));
	std::vector<std::pair<lumex::block_hash, lumex::root>> batch{ { send2->hash (), send2->root () }, { receive1->hash (), receive1->root () } };
	lumex::messages::confirm_req message{ lumex::dev::network_params.network, batch };
	auto channel = std::make_shared<lumex::transport::fake::channel> (node);
	// Generates and sends one vote for both hashes which is then cached
	node.inbound (message, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (lumex::stat::type::message, lumex::stat::detail::confirm_ack, lumex::stat::dir::out), 1);
	ASSERT_EQ (1, node.stats.count (lumex::stat::type::message, lumex::stat::detail::confirm_ack, lumex::stat::dir::out));
	ASSERT_FALSE (node.history.votes (send2->root (), send2->hash ()).empty ());
	ASSERT_FALSE (node.history.votes (receive1->root (), receive1->hash ()).empty ());
	// Only one confirm_ack should be sent if all hashes are part of the same vote
	node.inbound (message, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (lumex::stat::type::message, lumex::stat::detail::confirm_ack, lumex::stat::dir::out), 2);
	ASSERT_EQ (2, node.stats.count (lumex::stat::type::message, lumex::stat::detail::confirm_ack, lumex::stat::dir::out));
	// Test when votes are different
	node.history.erase (send2->root ());
	node.history.erase (receive1->root ());
	node.inbound (lumex::messages::confirm_req{ lumex::dev::network_params.network, send2->hash (), send2->root () }, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (lumex::stat::type::message, lumex::stat::detail::confirm_ack, lumex::stat::dir::out), 3);
	ASSERT_EQ (3, node.stats.count (lumex::stat::type::message, lumex::stat::detail::confirm_ack, lumex::stat::dir::out));
	node.inbound (lumex::messages::confirm_req{ lumex::dev::network_params.network, receive1->hash (), receive1->root () }, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (lumex::stat::type::message, lumex::stat::detail::confirm_ack, lumex::stat::dir::out), 4);
	ASSERT_EQ (4, node.stats.count (lumex::stat::type::message, lumex::stat::detail::confirm_ack, lumex::stat::dir::out));
	// There are two different votes, so both should be sent in response
	node.inbound (message, channel);
	ASSERT_TIMELY_EQ (3s, node.stats.count (lumex::stat::type::message, lumex::stat::detail::confirm_ack, lumex::stat::dir::out), 6);
	ASSERT_EQ (6, node.stats.count (lumex::stat::type::message, lumex::stat::detail::confirm_ack, lumex::stat::dir::out));
}

/**
 * There is a cache for locally generated votes. This test checks that the node
 * properly caches and uses those votes when replying to confirm_req requests.
 */
// TODO: Local vote cache is no longer used when generating votes
TEST (node, DISABLED_local_votes_cache_generate_new_vote)
{
	lumex::test::system system;
	lumex::node_config node_config;
	node_config.peering_port = system.get_available_port ();
	node_config.backlog_scan->enable = false;
	auto & node (*system.add_node (node_config));
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);

	// Send a confirm req for genesis block to node
	lumex::messages::confirm_req message1{ lumex::dev::network_params.network, lumex::dev::genesis->hash (), lumex::dev::genesis->root () };
	auto channel = std::make_shared<lumex::transport::fake::channel> (node);
	node.inbound (message1, channel);

	// check that the node generated a vote for the genesis block and that it is stored in the local vote cache and it is the only vote
	ASSERT_TIMELY (5s, !node.history.votes (lumex::dev::genesis->root (), lumex::dev::genesis->hash ()).empty ());
	auto votes1 = node.history.votes (lumex::dev::genesis->root (), lumex::dev::genesis->hash ());
	ASSERT_EQ (1, votes1.size ());
	ASSERT_EQ (1, votes1[0]->hashes.size ());
	ASSERT_EQ (lumex::dev::genesis->hash (), votes1[0]->hashes[0]);
	ASSERT_TIMELY_EQ (3s, node.stats.count (lumex::stat::type::vote_replier, lumex::stat::detail::reply_final), 1);

	auto send1 = lumex::state_block_builder ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (lumex::dev::genesis->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				 .link (lumex::dev::genesis_key.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (lumex::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (lumex::block_status::progress, node.process (send1));
	// One of the hashes is cached
	std::vector<std::pair<lumex::block_hash, lumex::root>> roots_hashes{ std::make_pair (lumex::dev::genesis->hash (), lumex::dev::genesis->root ()), std::make_pair (send1->hash (), send1->root ()) };
	lumex::messages::confirm_req message2{ lumex::dev::network_params.network, roots_hashes };
	node.inbound (message2, channel);
	ASSERT_TIMELY (3s, !node.history.votes (send1->root (), send1->hash ()).empty ());
	auto votes2 (node.history.votes (send1->root (), send1->hash ()));
	ASSERT_EQ (1, votes2.size ());
	ASSERT_EQ (1, votes2[0]->hashes.size ());
	ASSERT_TIMELY_EQ (3s, node.stats.count (lumex::stat::type::vote_replier, lumex::stat::detail::reply_final), 2);
	ASSERT_FALSE (node.history.votes (lumex::dev::genesis->root (), lumex::dev::genesis->hash ()).empty ());
	ASSERT_FALSE (node.history.votes (send1->root (), send1->hash ()).empty ());
	// First generated + again cached + new generated
	ASSERT_TIMELY_EQ (3s, 3, node.stats.count (lumex::stat::type::message, lumex::stat::detail::confirm_ack, lumex::stat::dir::out));
}

// TODO: Local vote cache is no longer used when generating votes
TEST (node, DISABLED_local_votes_cache_fork)
{
	lumex::test::system system;
	lumex::node_flags node_flags;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_bootstrap_bulk_pull_server = true;
	node_flags.disable_bootstrap_listener = true;
	node_flags.disable_lazy_bootstrap = true;
	node_flags.disable_legacy_bootstrap = true;
	node_flags.disable_wallet_bootstrap = true;
	lumex::node_config node_config;
	node_config.peering_port = system.get_available_port ();
	node_config.backlog_scan->enable = false;
	auto & node1 (*system.add_node (node_config, node_flags));
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	auto send1 = lumex::state_block_builder ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (lumex::dev::genesis->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				 .link (lumex::dev::genesis_key.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (lumex::dev::genesis->hash ()))
				 .build ();
	auto send1_fork = lumex::state_block_builder ()
					  .account (lumex::dev::genesis_key.pub)
					  .previous (lumex::dev::genesis->hash ())
					  .representative (lumex::dev::genesis_key.pub)
					  .balance (lumex::dev::constants.genesis_amount - 2 * lumex::Klumex_ratio)
					  .link (lumex::dev::genesis_key.pub)
					  .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					  .work (*node1.work_generate_blocking (lumex::dev::genesis->hash ()))
					  .build ();
	ASSERT_EQ (lumex::block_status::progress, node1.process (send1));
	// Cache vote
	auto vote = lumex::test::make_vote (lumex::dev::genesis_key, { send1 }, 0, 0);
	node1.vote_processor.vote (vote, std::make_shared<lumex::transport::fake::channel> (node1));
	node1.history.add (send1->root (), send1->hash (), vote);
	auto votes2 (node1.history.votes (send1->root (), send1->hash ()));
	ASSERT_EQ (1, votes2.size ());
	ASSERT_EQ (1, votes2[0]->hashes.size ());
	// Start election for forked block
	node_config.peering_port = system.get_available_port ();
	auto & node2 (*system.add_node (node_config, node_flags));
	node2.process_active (send1_fork);
	ASSERT_TIMELY (5s, node2.block_or_pruned_exists (send1->hash ()));
}

TEST (node, vote_republish)
{
	lumex::test::system system (2);
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];
	lumex::keypair key2;
	// by not setting a private key on node1's wallet for genesis account, it is stopped from voting
	system.wallet (1)->insert_adhoc (key2.prv);

	// send1 and send2 are forks of each other
	lumex::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<lumex::uint128_t>::max () - node1.config.receive_minimum.number ())
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<lumex::uint128_t>::max () - node1.config.receive_minimum.number () * 2)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();

	// process send1 first, this will make sure send1 goes into the ledger and an election is started
	node1.process_active (send1);
	ASSERT_TIMELY (5s, node2.block (send1->hash ()));
	ASSERT_TIMELY (5s, node1.active.active (*send1));
	ASSERT_TIMELY (5s, node2.active.active (*send1));

	// now process send2, send2 will not go in the ledger because only the first block of a fork goes in the ledger
	node1.process_active (send2);
	ASSERT_TIMELY (5s, node1.active.active (*send2));

	// send2 cannot be synced because it is not in the ledger of node1, it is only in the election object in RAM on node1
	ASSERT_FALSE (node1.block (send2->hash ()));

	// the vote causes the election to reach quorum and for the vote (and block?) to be published from node1 to node2
	auto vote = lumex::test::make_final_vote (lumex::dev::genesis_key, { send2 });
	node1.vote_processor.vote (vote, std::make_shared<lumex::transport::fake::channel> (node1));

	// FIXME: there is a race condition here, if the vote arrives before the block then the vote is wasted and the test fails
	// we could resend the vote but then there is a race condition between the vote resending and the election reaching quorum on node1
	// the proper fix would be to observe on node2 that both the block and the vote arrived in whatever order
	// the real node will do a confirm request if it needs to find a lost vote

	// check that send2 won on both nodes
	ASSERT_TIMELY (5s, node1.block_confirmed (send2->hash ()));
	ASSERT_TIMELY (5s, node2.block_confirmed (send2->hash ()));

	// check that send1 is deleted from the ledger on nodes
	ASSERT_FALSE (node1.block (send1->hash ()));
	ASSERT_FALSE (node2.block (send1->hash ()));
	ASSERT_TIMELY_EQ (5s, node2.balance (key2.pub), node1.config.receive_minimum.number () * 2);
	ASSERT_TIMELY_EQ (5s, node1.balance (key2.pub), node1.config.receive_minimum.number () * 2);
}

TEST (node, vote_by_hash_bundle)
{
	// Keep max_hashes above system to ensure it is kept in scope as votes can be added during system destruction
	std::atomic<size_t> max_hashes{ 0 };
	lumex::test::system system (1);
	auto & node = *system.nodes[0];
	lumex::state_block_builder builder;
	std::vector<std::shared_ptr<lumex::state_block>> blocks;
	auto block = builder.make_block ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (lumex::dev::genesis->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - 1)
				 .link (lumex::dev::genesis_key.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();
	blocks.push_back (block);
	ASSERT_EQ (lumex::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), blocks.back ()));
	for (auto i = 2; i < 200; ++i)
	{
		auto block = builder.make_block ()
					 .from (*blocks.back ())
					 .previous (blocks.back ()->hash ())
					 .balance (lumex::dev::constants.genesis_amount - i)
					 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					 .work (*system.work.generate (blocks.back ()->hash ()))
					 .build ();
		blocks.push_back (block);
		ASSERT_EQ (lumex::block_status::progress, node.ledger.process (node.ledger.tx_begin_write (), blocks.back ()));
	}

	// Confirming last block will confirm whole chain and allow us to generate votes for those blocks later
	lumex::test::confirm (node.ledger, blocks.back ());

	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	lumex::keypair key1;
	system.wallet (0)->insert_adhoc (key1.prv);

	system.nodes[0]->observers.vote.add ([&max_hashes] (std::shared_ptr<lumex::vote> const & vote_a, std::shared_ptr<lumex::transport::channel> const &, lumex::vote_source, lumex::vote_code) {
		if (vote_a->hashes.size () > max_hashes)
		{
			max_hashes = vote_a->hashes.size ();
		}
	});

	for (auto const & block : blocks)
	{
		system.nodes[0]->vote_generator.vote_normal (block->qualified_root (), block->hash (), 0);
	}

	// Verify that bundling occurs. While reaching 12 should be common on most hardware in release mode,
	// we set this low enough to allow the test to pass on CI/with sanitizers.
	ASSERT_TIMELY (20s, max_hashes.load () >= 3);
}

// This test places block send1 onto every node. Then it creates block send2 (which is a fork of send1) and sends it to node1.
// Then it sends a vote for send2 to node1 and expects node2 to also get the block plus vote and confirm send2.
// TODO: This test enforces the order block followed by vote on node1, should vote followed by block also work? It doesn't currently.
TEST (node, vote_by_hash_republish)
{
	lumex::test::system system{ 2 };
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];
	lumex::keypair key2;
	system.wallet (1)->insert_adhoc (key2.prv);

	// send1 and send2 are forks of each other
	lumex::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<lumex::uint128_t>::max () - node1.config.receive_minimum.number ())
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<lumex::uint128_t>::max () - node1.config.receive_minimum.number () * 2)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();

	// give block send1 to node1 and check that an election for send1 starts on both nodes
	node1.process_active (send1);
	ASSERT_TIMELY (5s, node1.active.active (*send1));
	ASSERT_TIMELY (5s, node2.active.active (*send1));

	// give block send2 to node1 and wait until the block is received and processed by node1
	node1.network.filter.clear ();
	node1.process_active (send2);
	ASSERT_TIMELY (5s, node1.active.active (*send2));

	// construct a vote for send2 in order to overturn send1
	std::vector<lumex::block_hash> vote_blocks;
	vote_blocks.push_back (send2->hash ());
	auto vote = lumex::test::make_final_vote (lumex::dev::genesis_key, { vote_blocks });
	node1.vote_processor.vote (vote, std::make_shared<lumex::transport::fake::channel> (node1));

	// send2 should win on both nodes
	ASSERT_TIMELY (5s, node1.block_confirmed (send2->hash ()));
	ASSERT_TIMELY (5s, node2.block_confirmed (send2->hash ()));
	ASSERT_FALSE (node1.block (send1->hash ()));
	ASSERT_FALSE (node2.block (send1->hash ()));
	ASSERT_TIMELY_EQ (5s, node2.balance (key2.pub), node1.config.receive_minimum.number () * 2);
	ASSERT_TIMELY_EQ (5s, node1.balance (key2.pub), node1.config.receive_minimum.number () * 2);
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/lumexcurrency/lumex-node/pull/3629
// Issue for investigating it: https://github.com/lumexcurrency/lumex-node/issues/3638
TEST (node, DISABLED_vote_by_hash_epoch_block_republish)
{
	lumex::test::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	lumex::keypair key2;
	system.wallet (1)->insert_adhoc (key2.prv);
	auto send1 = lumex::send_block_builder ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<lumex::uint128_t>::max () - node1.config.receive_minimum.number ())
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();
	auto epoch1 = lumex::state_block_builder ()
				  .account (lumex::dev::genesis_key.pub)
				  .previous (lumex::dev::genesis->hash ())
				  .representative (lumex::dev::genesis_key.pub)
				  .balance (lumex::dev::constants.genesis_amount)
				  .link (node1.ledger.epoch_link (lumex::epoch::epoch_1))
				  .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				  .work (*system.work.generate (lumex::dev::genesis->hash ()))
				  .build ();
	node1.process_active (send1);
	ASSERT_TIMELY (5s, node2.active.active (*send1));
	node1.active.publish (epoch1);
	std::vector<lumex::block_hash> vote_blocks;
	vote_blocks.push_back (epoch1->hash ());
	auto vote = lumex::test::make_vote (lumex::dev::genesis_key, { vote_blocks }, 0, 0);
	ASSERT_TRUE (node1.active.active (*send1));
	ASSERT_TRUE (node2.active.active (*send1));
	node1.vote_processor.vote (vote, std::make_shared<lumex::transport::fake::channel> (node1));
	ASSERT_TIMELY (10s, node1.block (epoch1->hash ()));
	ASSERT_TIMELY (10s, node2.block (epoch1->hash ()));
	ASSERT_FALSE (node1.block (send1->hash ()));
	ASSERT_FALSE (node2.block (send1->hash ()));
}

TEST (node, epoch_conflict_confirm)
{
	lumex::test::system system;
	lumex::node_config node_config;
	node_config.peering_port = system.get_available_port ();
	node_config.backlog_scan->enable = false;
	auto & node0 = *system.add_node (node_config);
	node_config.peering_port = system.get_available_port ();
	auto & node1 = *system.add_node (node_config);
	lumex::keypair key;
	lumex::keypair epoch_signer (lumex::dev::genesis_key);
	lumex::state_block_builder builder;

	// Node 1 is the voting node
	// Send sends to an account we control: send -> open -> change
	// Send2 sends to an account with public key of the open block
	// Epoch open qualified root: (open, 0) on account with the same public key as the hash of the open block
	// Epoch open and change have the same root!

	auto send = builder.make_block ()
				.account (lumex::dev::genesis_key.pub)
				.previous (lumex::dev::genesis->hash ())
				.representative (lumex::dev::genesis_key.pub)
				.balance (lumex::dev::constants.genesis_amount - 1)
				.link (key.pub)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*system.work.generate (lumex::dev::genesis->hash ()))
				.build ();
	auto open = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.balance (1)
				.link (send->hash ())
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build ();
	auto change = builder.make_block ()
				  .account (key.pub)
				  .previous (open->hash ())
				  .representative (key.pub)
				  .balance (1)
				  .link (0)
				  .sign (key.prv, key.pub)
				  .work (*system.work.generate (open->hash ()))
				  .build ();
	auto send2 = builder.make_block ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (send->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - 2)
				 .link (open->hash ())
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (send->hash ()))
				 .build ();
	auto epoch_open = builder.make_block ()
					  .account (change->root ().as_account ())
					  .previous (0)
					  .representative (0)
					  .balance (0)
					  .link (node0.ledger.epoch_link (lumex::epoch::epoch_1))
					  .sign (epoch_signer.prv, epoch_signer.pub)
					  .work (*system.work.generate (open->hash ()))
					  .build ();

	// Process initial blocks
	ASSERT_TRUE (lumex::test::process (node0, lumex::test::clone ({ send, send2, open })));
	ASSERT_TRUE (lumex::test::process (node1, lumex::test::clone ({ send, send2, open })));

	// Process conflicting blocks on nodes as blocks coming from live network
	ASSERT_TRUE (lumex::test::process_live (node0, lumex::test::clone ({ change, epoch_open })));
	ASSERT_TRUE (lumex::test::process_live (node1, lumex::test::clone ({ change, epoch_open })));

	// Ensure blocks were propagated to both nodes
	ASSERT_TIMELY (5s, lumex::test::exists (node0, { change, epoch_open }));
	ASSERT_TIMELY (5s, lumex::test::exists (node1, { change, epoch_open }));

	// Confirm initial blocks in node1 to allow generating votes later
	lumex::test::confirm (node1, { change, epoch_open, send2 });
	ASSERT_TIMELY (5s, lumex::test::confirmed (node1, { change, epoch_open, send2 }));

	// Start elections on node0 for conflicting change and epoch_open blocks (these two blocks have the same root)
	ASSERT_TRUE (lumex::test::activate (node0, { change, epoch_open }));
	ASSERT_TIMELY (5s, lumex::test::active (node0, { change, epoch_open }));

	// Make node1 a representative so it can vote for both blocks
	system.wallet (1)->insert_adhoc (lumex::dev::genesis_key.prv);

	// Ensure the elections for conflicting blocks have started
	ASSERT_TIMELY (5s, lumex::test::active (node0, { change, epoch_open }));

	// Ensure both conflicting blocks were successfully processed and confirmed
	ASSERT_TIMELY (5s, lumex::test::confirmed (node0, { change, epoch_open }));
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/lumexcurrency/lumex-node/pull/3526
// Issue for investigating it: https://github.com/lumexcurrency/lumex-node/issues/3527
TEST (node, DISABLED_fork_invalid_block_signature)
{
	lumex::test::system system;
	lumex::node_config config = system.default_config ();
	// Disabling republishing + waiting for a rollback before sending the correct vote below fixes an intermittent failure in this test
	// If these are taken out, one of two things may cause the test two fail often:
	// - Block *send2* might get processed before the rollback happens, simply due to timings, with code "fork", and not be processed again. Waiting for the rollback fixes this issue.
	// - Block *send1* might get processed again after the rollback happens, which causes *send2* to be processed with code "fork". Disabling block republishing ensures "send1" is not processed again.
	// An alternative would be to repeatedly flood the correct vote
	config.local_block_broadcaster->enable = false;
	auto & node1 (*system.add_node (config));
	auto & node2 (*system.add_node (config));
	lumex::keypair key2;
	lumex::send_block_builder builder;
	auto send1 = builder.make_block ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<lumex::uint128_t>::max () - node1.config.receive_minimum.number ())
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (std::numeric_limits<lumex::uint128_t>::max () - node1.config.receive_minimum.number () * 2)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();
	auto send2_corrupt (std::make_shared<lumex::send_block> (*send2));
	send2_corrupt->signature = lumex::signature (123);
	auto vote = lumex::test::make_vote (lumex::dev::genesis_key, { send2 }, 0, 0);
	auto vote_corrupt = lumex::test::make_vote (lumex::dev::genesis_key, { send2_corrupt }, 0, 0);

	node1.process_active (send1);
	ASSERT_TIMELY (5s, node1.block (send1->hash ()));
	// Send the vote with the corrupt block signature
	ASSERT_TRUE (node2.network.flood_vote_all (vote_corrupt, lumex::transport::traffic_type::test));
	// Wait for the rollback
	ASSERT_TIMELY (5s, node1.stats.count (lumex::stat::type::rollback));
	// Send the vote with the correct block
	ASSERT_TRUE (node2.network.flood_vote_all (vote, lumex::transport::traffic_type::test));
	ASSERT_TIMELY (10s, !node1.block (send1->hash ()));
	ASSERT_TIMELY (10s, node1.block (send2->hash ()));
	ASSERT_EQ (node1.block (send2->hash ())->block_signature (), send2->block_signature ());
}

TEST (node, fork_election_invalid_block_signature)
{
	lumex::test::system system (1);
	auto & node1 (*system.nodes[0]);
	lumex::block_builder builder;
	auto send1 = builder.state ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (lumex::dev::genesis->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				 .link (lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .build ();
	auto send2 = builder.state ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (lumex::dev::genesis->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - 2 * lumex::Klumex_ratio)
				 .link (lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .build ();
	auto send3 = builder.state ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (lumex::dev::genesis->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - 2 * lumex::Klumex_ratio)
				 .link (lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .sign (lumex::dev::genesis_key.prv, 0) // Invalid signature
				 .build ();

	auto channel1 = std::make_shared<lumex::transport::fake::channel> (node1);
	node1.inbound (lumex::messages::publish{ lumex::dev::network_params.network, send1 }, channel1);
	ASSERT_TIMELY (5s, node1.active.active (send1->qualified_root ()));
	auto election (node1.active.election (send1->qualified_root ()));
	ASSERT_NE (nullptr, election);
	ASSERT_EQ (1, election->blocks ().size ());
	node1.inbound (lumex::messages::publish{ lumex::dev::network_params.network, send3 }, channel1);
	node1.inbound (lumex::messages::publish{ lumex::dev::network_params.network, send2 }, channel1);
	ASSERT_TIMELY (3s, election->blocks ().size () > 1);
	ASSERT_EQ (election->blocks ()[send2->hash ()]->block_signature (), send2->block_signature ());
}

TEST (node, block_processor_signatures)
{
	lumex::test::system system{ 1 };
	auto & node1 = *system.nodes[0];
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	lumex::block_hash latest = system.nodes[0]->latest (lumex::dev::genesis_key.pub);
	lumex::state_block_builder builder;
	lumex::keypair key1;
	lumex::keypair key2;
	lumex::keypair key3;
	auto send1 = builder.make_block ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (latest)
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				 .link (key1.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (latest))
				 .build ();
	auto send2 = builder.make_block ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - 2 * lumex::Klumex_ratio)
				 .link (key2.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (send1->hash ()))
				 .build ();
	auto send3 = builder.make_block ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (send2->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - 3 * lumex::Klumex_ratio)
				 .link (key3.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (send2->hash ()))
				 .build ();
	// Invalid signature bit
	auto send4 = builder.make_block ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (send3->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - 4 * lumex::Klumex_ratio)
				 .link (key3.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (send3->hash ()))
				 .build ();
	send4->signature.bytes[32] ^= 0x1;
	// Invalid signature bit (force)
	auto send5 = builder.make_block ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (send3->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - 5 * lumex::Klumex_ratio)
				 .link (key3.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (send3->hash ()))
				 .build ();
	send5->signature.bytes[32] ^= 0x1;
	// Invalid signature to unchecked
	node1.unchecked.put (send5->previous (), lumex::unchecked_info{ send5 });
	auto receive1 = builder.make_block ()
					.account (key1.pub)
					.previous (0)
					.representative (lumex::dev::genesis_key.pub)
					.balance (lumex::Klumex_ratio)
					.link (send1->hash ())
					.sign (key1.prv, key1.pub)
					.work (*node1.work_generate_blocking (key1.pub))
					.build ();
	auto receive2 = builder.make_block ()
					.account (key2.pub)
					.previous (0)
					.representative (lumex::dev::genesis_key.pub)
					.balance (lumex::Klumex_ratio)
					.link (send2->hash ())
					.sign (key2.prv, key2.pub)
					.work (*node1.work_generate_blocking (key2.pub))
					.build ();
	// Invalid private key
	auto receive3 = builder.make_block ()
					.account (key3.pub)
					.previous (0)
					.representative (lumex::dev::genesis_key.pub)
					.balance (lumex::Klumex_ratio)
					.link (send3->hash ())
					.sign (key2.prv, key3.pub)
					.work (*node1.work_generate_blocking (key3.pub))
					.build ();
	node1.process_active (send1);
	node1.process_active (send2);
	node1.process_active (send3);
	node1.process_active (send4);
	node1.process_active (receive1);
	node1.process_active (receive2);
	node1.process_active (receive3);
	ASSERT_TIMELY (5s, node1.block (receive2->hash ()) != nullptr); // Implies send1, send2, send3, receive1.
	ASSERT_TIMELY_EQ (5s, node1.unchecked.count (), 0);
	ASSERT_EQ (nullptr, node1.block (receive3->hash ())); // Invalid signer
	ASSERT_EQ (nullptr, node1.block (send4->hash ())); // Invalid signature via process_active
	ASSERT_EQ (nullptr, node1.block (send5->hash ())); // Invalid signature via unchecked
}

/*
 *  State blocks go through a different signature path, ensure invalidly signed state blocks are rejected
 *  This test can freeze if the wake conditions in block_processor::flush are off, for that reason this is done async here
 */
TEST (node, block_processor_reject_state)
{
	lumex::test::system system (1);
	auto & node (*system.nodes[0]);
	lumex::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (lumex::dev::genesis->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				 .link (lumex::dev::genesis_key.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (lumex::dev::genesis->hash ()))
				 .build ();
	send1->signature.bytes[0] ^= 1;
	ASSERT_FALSE (node.block_or_pruned_exists (send1->hash ()));
	node.process_active (send1);
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (lumex::stat::type::block_processor_result, lumex::stat::detail::bad_signature));
	ASSERT_FALSE (node.block_or_pruned_exists (send1->hash ()));
	auto send2 = builder.make_block ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (lumex::dev::genesis->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - 2 * lumex::Klumex_ratio)
				 .link (lumex::dev::genesis_key.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*node.work_generate_blocking (lumex::dev::genesis->hash ()))
				 .build ();
	node.process_active (send2);
	ASSERT_TIMELY (5s, node.block_or_pruned_exists (send2->hash ()));
}

TEST (node, confirm_back)
{
	lumex::test::system system (1);
	lumex::keypair key;
	auto & node (*system.nodes[0]);
	auto genesis_start_balance (node.balance (lumex::dev::genesis_key.pub));
	auto send1 = lumex::send_block_builder ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key.pub)
				 .balance (genesis_start_balance - 1)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();
	lumex::state_block_builder builder;
	auto open = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.balance (1)
				.link (send1->hash ())
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build ();
	auto send2 = builder.make_block ()
				 .account (key.pub)
				 .previous (open->hash ())
				 .representative (key.pub)
				 .balance (0)
				 .link (lumex::dev::genesis_key.pub)
				 .sign (key.prv, key.pub)
				 .work (*system.work.generate (open->hash ()))
				 .build ();
	node.process_active (send1);
	node.process_active (open);
	node.process_active (send2);
	ASSERT_TIMELY (5s, node.block (send2->hash ()) != nullptr);
	ASSERT_TRUE (lumex::test::start_elections (system, node, { send1, open, send2 }));
	ASSERT_EQ (3, node.active.size ());
	std::vector<lumex::block_hash> vote_blocks;
	vote_blocks.push_back (send2->hash ());
	auto vote = lumex::test::make_final_vote (lumex::dev::genesis_key, { vote_blocks });
	node.vote_processor.vote_blocking (vote, std::make_shared<lumex::transport::fake::channel> (node));
	ASSERT_TIMELY (10s, node.active.empty ());
}

TEST (node, peers)
{
	lumex::test::system system (1);
	auto node1 (system.nodes[0]);
	ASSERT_TRUE (node1->network.empty ());

	auto node2 (std::make_shared<lumex::node> (system.get_available_port (), lumex::unique_path (), system.work, lumex::node_flags{}));
	system.nodes.push_back (node2);

	auto endpoint = node1->network.endpoint ();
	lumex::endpoint_key endpoint_key{ endpoint.address ().to_v6 ().to_bytes (), endpoint.port () };
	auto & store = node2->store;
	{
		// Add a peer to the database
		auto transaction (store.tx_begin_write ());
		store.peer.put (transaction, endpoint_key, 37);

		// Add a peer which is not contactable
		store.peer.put (transaction, lumex::endpoint_key{ boost::asio::ip::address_v6::any ().to_bytes (), 55555 }, 42);
	}

	node2->start ();
	ASSERT_TIMELY (10s, !node2->network.empty () && !node1->network.empty ())
	// Wait to finish TCP node ID handshakes
	ASSERT_TIMELY (10s, node1->tcp_listener.realtime_count () != 0 && node2->tcp_listener.realtime_count () != 0);
	// Confirm that the peers match with the endpoints we are expecting
	ASSERT_EQ (1, node1->network.size ());
	auto list1 (node1->network.list (2));
	ASSERT_EQ (node2->get_node_id (), list1[0]->get_node_id ());
	ASSERT_EQ (lumex::transport::transport_type::tcp, list1[0]->get_type ());
	ASSERT_EQ (1, node2->network.size ());
	auto list2 (node2->network.list (2));
	ASSERT_EQ (node1->get_node_id (), list2[0]->get_node_id ());
	ASSERT_EQ (lumex::transport::transport_type::tcp, list2[0]->get_type ());

	// Uncontactable peer should not be stored
	ASSERT_TIMELY_EQ (5s, store.peer.count (store.tx_begin_read ()), 1);
	ASSERT_TRUE (store.peer.exists (store.tx_begin_read (), endpoint_key));

	// Stop the peer node and check that it is removed from the store
	system.stop_node (*node1);

	// TODO: In `tcp_channels::store_all` we skip store operation when there are no peers present,
	// so the best we can do here is check if network is empty
	ASSERT_TIMELY (10s, node2->network.empty ());
}

TEST (node, peer_history_restart)
{
	lumex::test::system system (1);
	auto node1 (system.nodes[0]);
	ASSERT_TRUE (node1->network.empty ());
	auto endpoint = node1->network.endpoint ();
	lumex::endpoint_key endpoint_key{ endpoint.address ().to_v6 ().to_bytes (), endpoint.port () };
	auto path (lumex::unique_path ());
	{
		auto node2 (std::make_shared<lumex::node> (system.get_available_port (), path, system.work, lumex::node_flags{}));
		system.nodes.push_back (node2);
		auto & store = node2->store;
		{
			// Add a peer to the database
			auto transaction (store.tx_begin_write ());
			store.peer.put (transaction, endpoint_key, 37);
		}
		node2->start ();
		ASSERT_TIMELY (10s, !node2->network.empty ());
		// Confirm that the peers match with the endpoints we are expecting
		auto list (node2->network.list (2));
		ASSERT_EQ (node1->network.endpoint (), list[0]->get_remote_endpoint ());
		ASSERT_EQ (1, node2->network.size ());
		system.stop_node (*node2);
	}
	// Restart node
	{
		lumex::node_flags node_flags;
		node_flags.read_only = true;
		auto node3 (std::make_shared<lumex::node> (system.get_available_port (), path, system.work, node_flags));
		system.nodes.push_back (node3);
		// Check cached peers after restart
		node3->network.start ();
		node3->add_initial_peers ();

		auto & store = node3->store;
		{
			auto transaction (store.tx_begin_read ());
			ASSERT_EQ (store.peer.count (transaction), 1);
			ASSERT_TRUE (store.peer.exists (transaction, endpoint_key));
		}
		ASSERT_TIMELY (10s, !node3->network.empty ());
		// Confirm that the peers match with the endpoints we are expecting
		auto list (node3->network.list (2));
		ASSERT_EQ (node1->network.endpoint (), list[0]->get_remote_endpoint ());
		ASSERT_EQ (1, node3->network.size ());
		system.stop_node (*node3);
	}
}

/** This checks that a node can be opened (without being blocked) when a write lock is held elsewhere */
TEST (node, dont_write_lock_node)
{
	// RocksDB does not support opening the same database from multiple instances
	// TODO: Implement a guard mechanism to prevent multiple node instances from opening the same RocksDB database
	if (lumex::default_database_backend () == lumex::database_backend::rocksdb)
	{
		GTEST_SKIP ();
	}

	auto path = lumex::unique_path ();

	std::promise<void> write_lock_held_promise;
	std::promise<void> finished_promise;
	std::thread ([&path, &write_lock_held_promise, &finished_promise] () {
		auto store = lumex::test::make_store (path);
		{
			auto transaction (store->tx_begin_write ());
			lumex::ledger::seed_genesis (*store, transaction, lumex::dev::constants);
		}

		// Hold write lock open until main thread is done needing it
		auto transaction (store->tx_begin_write ());
		write_lock_held_promise.set_value ();
		finished_promise.get_future ().wait ();
	})
	.detach ();

	write_lock_held_promise.get_future ().wait ();

	// Check inactive node can finish executing while a write lock is open
	lumex::inactive_node node (path, lumex::inactive_node_flag_defaults ());
	finished_promise.set_value ();
}

TEST (node, bidirectional_tcp)
{
	lumex::test::system system;
	lumex::node_flags node_flags;
	// Disable bootstrap to start elections for new blocks
	node_flags.disable_legacy_bootstrap = true;
	node_flags.disable_lazy_bootstrap = true;
	node_flags.disable_wallet_bootstrap = true;
	lumex::node_config node_config;
	node_config.peering_port = system.get_available_port ();
	node_config.backlog_scan->enable = false;
	auto node1 = system.add_node (node_config, node_flags);
	node_config.peering_port = system.get_available_port ();
	node_config.tcp->max_inbound_connections = 0; // Disable incoming TCP connections for node 2
	auto node2 = system.add_node (node_config, node_flags);
	// Check network connections
	ASSERT_EQ (1, node1->network.size ());
	ASSERT_EQ (1, node2->network.size ());
	auto list1 (node1->network.list (1));
	ASSERT_EQ (lumex::transport::transport_type::tcp, list1[0]->get_type ());
	ASSERT_NE (node2->network.endpoint (), list1[0]->get_remote_endpoint ()); // Ephemeral port
	ASSERT_EQ (node2->get_node_id (), list1[0]->get_node_id ());
	auto list2 (node2->network.list (1));
	ASSERT_EQ (lumex::transport::transport_type::tcp, list2[0]->get_type ());
	ASSERT_EQ (node1->network.endpoint (), list2[0]->get_remote_endpoint ());
	ASSERT_EQ (node1->get_node_id (), list2[0]->get_node_id ());
	// Test block propagation from node 1
	lumex::keypair key;
	lumex::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (lumex::dev::genesis->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				 .link (key.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*node1->work_generate_blocking (lumex::dev::genesis->hash ()))
				 .build ();
	node1->process_active (send1);
	ASSERT_TIMELY (10s, node1->block_or_pruned_exists (send1->hash ()) && node2->block_or_pruned_exists (send1->hash ()));
	// Test block confirmation from node 1 (add representative to node 1)
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	// Wait to find new reresentative
	ASSERT_TIMELY (10s, node2->rep_crawler.representative_count () != 0);
	/* Wait for confirmation
	To check connection we need only node 2 confirmation status
	Node 1 election can be unconfirmed because representative private key was inserted after election start (and node 2 isn't flooding new votes to principal representatives) */
	bool confirmed (false);
	system.deadline_set (10s);
	while (!confirmed)
	{
		auto transaction2 = node2->ledger.tx_begin_read ();
		confirmed = node2->ledger.cemented.block_exists_or_pruned (transaction2, send1->hash ());
		ASSERT_NO_ERROR (system.poll ());
	}
	// Test block propagation & confirmation from node 2 (remove representative from node 1)
	system.wallet (0)->remove_account (lumex::dev::genesis_key.pub);
	/* Test block propagation from node 2
	Node 2 has only ephemeral TCP port open. Node 1 cannot establish connection to node 2 listening port */
	auto send2 = builder.make_block ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - 2 * lumex::Klumex_ratio)
				 .link (key.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*node1->work_generate_blocking (send1->hash ()))
				 .build ();
	node2->process_active (send2);
	ASSERT_TIMELY (10s, node1->block_or_pruned_exists (send2->hash ()) && node2->block_or_pruned_exists (send2->hash ()));
	// Test block confirmation from node 2 (add representative to node 2)
	system.wallet (1)->insert_adhoc (lumex::dev::genesis_key.prv);
	// Wait to find changed reresentative
	ASSERT_TIMELY (10s, node1->rep_crawler.representative_count () != 0);
	/* Wait for confirmation
	To check connection we need only node 1 confirmation status
	Node 2 election can be unconfirmed because representative private key was inserted after election start (and node 1 isn't flooding new votes to principal representatives) */
	confirmed = false;
	system.deadline_set (20s);
	while (!confirmed)
	{
		auto transaction1 = node1->ledger.tx_begin_read ();
		confirmed = node1->ledger.cemented.block_exists_or_pruned (transaction1, send2->hash ());
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, node_sequence)
{
	lumex::test::system system (3);
	ASSERT_EQ (0, system.nodes[0]->node_seq);
	ASSERT_EQ (0, system.nodes[0]->node_seq);
	ASSERT_EQ (1, system.nodes[1]->node_seq);
	ASSERT_EQ (2, system.nodes[2]->node_seq);
}

/**
 * This test checks that a node can generate a self generated vote to rollback an election.
 * It also checks that the vote aggregrator replies with the election winner at the time.
 */
TEST (node, rollback_vote_self)
{
	lumex::test::system system;
	lumex::node_flags flags;
	flags.disable_request_loop = true;
	auto & node = *system.add_node (flags);
	lumex::state_block_builder builder;
	lumex::keypair key;

	// send half the voting weight to a non voting rep to ensure quorum cannot be reached
	auto send1 = builder.make_block ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (lumex::dev::genesis->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (lumex::dev::constants.genesis_amount - (lumex::dev::constants.genesis_amount / 2))
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();

	auto open = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.link (send1->hash ())
				.balance (lumex::dev::constants.genesis_amount / 2)
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build ();

	// send 1 raw
	auto send2 = builder.make_block ()
				 .from (*send1)
				 .previous (send1->hash ())
				 .balance (send1->balance_field ().value ().number () - 1)
				 .link (lumex::dev::genesis_key.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();

	// fork of send2 block
	auto fork = builder.make_block ()
				.from (*send2)
				.balance (send1->balance_field ().value ().number () - 2)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.build ();

	// Process and mark the first 2 blocks as confirmed to allow voting
	ASSERT_TRUE (lumex::test::process (node, { send1, open }));
	lumex::test::confirm (node.ledger, open);

	// wait until the rep weights have caught up with the weight transfer
	ASSERT_TIMELY_EQ (5s, lumex::dev::constants.genesis_amount / 2, node.weight (key.pub));

	// process forked blocks, send2 will be the winner because it was first and there are no votes yet
	node.process_active (send2);
	std::shared_ptr<lumex::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (send2->qualified_root ()));
	node.process_active (fork);
	ASSERT_TIMELY_EQ (5s, 2, election->blocks ().size ());
	ASSERT_EQ (election->winner ()->hash (), send2->hash ());

	{
		// The write guard prevents the block processor from performing the rollback
		auto write_guard = node.store.write_queue.wait (lumex::store::writer::testing);

		ASSERT_EQ (0, election->votes_with_weight ().size ());
		// Vote with key to switch the winner
		election->vote (key.pub, 0, fork->hash (), lumex::vote_source::live);
		ASSERT_EQ (1, election->votes_with_weight ().size ());
		// The winner changed
		ASSERT_EQ (election->winner ()->hash (), fork->hash ());

		// Insert genesis key in the wallet
		system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);

		// Without the rollback being finished, the vote replier should not reply with any vote
		node.vote_replier.request ({ { send2->hash (), send2->root () } }, node.loopback_channel);
		ASSERT_ALWAYS (1s, !election->votes ().contains (lumex::dev::genesis_key.pub));

		// Going out of the scope allows the rollback to complete
	}

	// A vote is eventually generated from the local representative
	auto is_genesis_vote = [] (lumex::vote_with_weight_info info) {
		return info.representative == lumex::dev::genesis_key.pub;
	};
	ASSERT_TIMELY_EQ (5s, 2, election->votes_with_weight ().size ());
	auto votes_with_weight = election->votes_with_weight ();
	ASSERT_EQ (1, std::count_if (votes_with_weight.begin (), votes_with_weight.end (), is_genesis_vote));
	auto vote = std::find_if (votes_with_weight.begin (), votes_with_weight.end (), is_genesis_vote);
	ASSERT_NE (votes_with_weight.end (), vote);
	ASSERT_EQ (fork->hash (), vote->hash);
}

TEST (node, rollback_gap_source)
{
	lumex::test::system system;
	lumex::node_config node_config;
	node_config.peering_port = system.get_available_port ();
	node_config.backlog_scan->enable = false;
	auto & node = *system.add_node (node_config);
	lumex::state_block_builder builder;
	lumex::keypair key;
	auto send1 = builder.make_block ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (lumex::dev::genesis->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (lumex::dev::constants.genesis_amount - 1)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();
	// Side a of a forked open block receiving from send1
	// This is a losing block
	auto fork1a = builder.make_block ()
				  .account (key.pub)
				  .previous (0)
				  .representative (key.pub)
				  .link (send1->hash ())
				  .balance (1)
				  .sign (key.prv, key.pub)
				  .work (*system.work.generate (key.pub))
				  .build ();
	auto send2 = builder.make_block ()
				 .from (*send1)
				 .previous (send1->hash ())
				 .balance (send1->balance_field ().value ().number () - 1)
				 .link (key.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	// Side b of a forked open block receiving from send2.
	// This is the winning block
	auto fork1b = builder.make_block ()
				  .from (*fork1a)
				  .link (send2->hash ())
				  .sign (key.prv, key.pub)
				  .build ();
	// Set 'node' up with losing block 'fork1a'
	ASSERT_EQ (lumex::block_status::progress, node.process (send1));
	ASSERT_EQ (lumex::block_status::progress, node.process (fork1a));
	// Node has 'fork1a' & doesn't have source 'send2' for winning 'fork1b' block
	ASSERT_EQ (nullptr, node.block (send2->hash ()));
	node.block_processor.force (fork1b);
	ASSERT_TIMELY_EQ (5s, node.block (fork1a->hash ()), nullptr);
	// Wait for the rollback (attempt to replace fork with open)
	ASSERT_TIMELY_EQ (5s, node.stats.count (lumex::stat::type::rollback, lumex::stat::detail::open), 1);
	// But replacing is not possible (missing source block - send2)
	ASSERT_EQ (nullptr, node.block (fork1b->hash ()));
	// Fork can be returned by some other forked node
	node.process_active (fork1a);
	ASSERT_TIMELY (5s, node.block (fork1a->hash ()) != nullptr);
	// With send2 block in ledger election can start again to remove fork block
	ASSERT_EQ (lumex::block_status::progress, node.process (send2));
	node.block_processor.force (fork1b);
	// Wait for new rollback
	ASSERT_TIMELY_EQ (5s, node.stats.count (lumex::stat::type::rollback, lumex::stat::detail::open), 2);
	// Now fork block should be replaced with open
	ASSERT_TIMELY (5s, node.block (fork1b->hash ()) != nullptr);
	ASSERT_EQ (nullptr, node.block (fork1a->hash ()));
}

// Confirm a complex dependency graph starting from the first block
TEST (node, dependency_graph)
{
	lumex::test::system system;
	lumex::node_config config;
	config.peering_port = system.get_available_port ();
	config.backlog_scan->enable = false;
	auto & node = *system.add_node (config);

	lumex::state_block_builder builder;
	lumex::keypair key1, key2, key3;

	// Send to key1
	auto gen_send1 = builder.make_block ()
					 .account (lumex::dev::genesis_key.pub)
					 .previous (lumex::dev::genesis->hash ())
					 .representative (lumex::dev::genesis_key.pub)
					 .link (key1.pub)
					 .balance (lumex::dev::constants.genesis_amount - 1)
					 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					 .work (*system.work.generate (lumex::dev::genesis->hash ()))
					 .build ();
	// Receive from genesis
	auto key1_open = builder.make_block ()
					 .account (key1.pub)
					 .previous (0)
					 .representative (key1.pub)
					 .link (gen_send1->hash ())
					 .balance (1)
					 .sign (key1.prv, key1.pub)
					 .work (*system.work.generate (key1.pub))
					 .build ();
	// Send to genesis
	auto key1_send1 = builder.make_block ()
					  .account (key1.pub)
					  .previous (key1_open->hash ())
					  .representative (key1.pub)
					  .link (lumex::dev::genesis_key.pub)
					  .balance (0)
					  .sign (key1.prv, key1.pub)
					  .work (*system.work.generate (key1_open->hash ()))
					  .build ();
	// Receive from key1
	auto gen_receive = builder.make_block ()
					   .from (*gen_send1)
					   .previous (gen_send1->hash ())
					   .link (key1_send1->hash ())
					   .balance (lumex::dev::constants.genesis_amount)
					   .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					   .work (*system.work.generate (gen_send1->hash ()))
					   .build ();
	// Send to key2
	auto gen_send2 = builder.make_block ()
					 .from (*gen_receive)
					 .previous (gen_receive->hash ())
					 .link (key2.pub)
					 .balance (gen_receive->balance_field ().value ().number () - 2)
					 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					 .work (*system.work.generate (gen_receive->hash ()))
					 .build ();
	// Receive from genesis
	auto key2_open = builder.make_block ()
					 .account (key2.pub)
					 .previous (0)
					 .representative (key2.pub)
					 .link (gen_send2->hash ())
					 .balance (2)
					 .sign (key2.prv, key2.pub)
					 .work (*system.work.generate (key2.pub))
					 .build ();
	// Send to key3
	auto key2_send1 = builder.make_block ()
					  .account (key2.pub)
					  .previous (key2_open->hash ())
					  .representative (key2.pub)
					  .link (key3.pub)
					  .balance (1)
					  .sign (key2.prv, key2.pub)
					  .work (*system.work.generate (key2_open->hash ()))
					  .build ();
	// Receive from key2
	auto key3_open = builder.make_block ()
					 .account (key3.pub)
					 .previous (0)
					 .representative (key3.pub)
					 .link (key2_send1->hash ())
					 .balance (1)
					 .sign (key3.prv, key3.pub)
					 .work (*system.work.generate (key3.pub))
					 .build ();
	// Send to key1
	auto key2_send2 = builder.make_block ()
					  .from (*key2_send1)
					  .previous (key2_send1->hash ())
					  .link (key1.pub)
					  .balance (key2_send1->balance_field ().value ().number () - 1)
					  .sign (key2.prv, key2.pub)
					  .work (*system.work.generate (key2_send1->hash ()))
					  .build ();
	// Receive from key2
	auto key1_receive = builder.make_block ()
						.from (*key1_send1)
						.previous (key1_send1->hash ())
						.link (key2_send2->hash ())
						.balance (key1_send1->balance_field ().value ().number () + 1)
						.sign (key1.prv, key1.pub)
						.work (*system.work.generate (key1_send1->hash ()))
						.build ();
	// Send to key3
	auto key1_send2 = builder.make_block ()
					  .from (*key1_receive)
					  .previous (key1_receive->hash ())
					  .link (key3.pub)
					  .balance (key1_receive->balance_field ().value ().number () - 1)
					  .sign (key1.prv, key1.pub)
					  .work (*system.work.generate (key1_receive->hash ()))
					  .build ();
	// Receive from key1
	auto key3_receive = builder.make_block ()
						.from (*key3_open)
						.previous (key3_open->hash ())
						.link (key1_send2->hash ())
						.balance (key3_open->balance_field ().value ().number () + 1)
						.sign (key3.prv, key3.pub)
						.work (*system.work.generate (key3_open->hash ()))
						.build ();
	// Upgrade key3
	auto key3_epoch = builder.make_block ()
					  .from (*key3_receive)
					  .previous (key3_receive->hash ())
					  .link (node.ledger.epoch_link (lumex::epoch::epoch_1))
					  .balance (key3_receive->balance_field ().value ())
					  .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					  .work (*system.work.generate (key3_receive->hash ()))
					  .build ();

	ASSERT_EQ (lumex::block_status::progress, node.process (gen_send1));
	ASSERT_EQ (lumex::block_status::progress, node.process (key1_open));
	ASSERT_EQ (lumex::block_status::progress, node.process (key1_send1));
	ASSERT_EQ (lumex::block_status::progress, node.process (gen_receive));
	ASSERT_EQ (lumex::block_status::progress, node.process (gen_send2));
	ASSERT_EQ (lumex::block_status::progress, node.process (key2_open));
	ASSERT_EQ (lumex::block_status::progress, node.process (key2_send1));
	ASSERT_EQ (lumex::block_status::progress, node.process (key3_open));
	ASSERT_EQ (lumex::block_status::progress, node.process (key2_send2));
	ASSERT_EQ (lumex::block_status::progress, node.process (key1_receive));
	ASSERT_EQ (lumex::block_status::progress, node.process (key1_send2));
	ASSERT_EQ (lumex::block_status::progress, node.process (key3_receive));
	ASSERT_EQ (lumex::block_status::progress, node.process (key3_epoch));
	ASSERT_TRUE (node.active.empty ());

	// Hash -> Ancestors
	std::unordered_map<lumex::block_hash, std::vector<lumex::block_hash>> dependency_graph{
		{ key1_open->hash (), { gen_send1->hash () } },
		{ key1_send1->hash (), { key1_open->hash () } },
		{ gen_receive->hash (), { gen_send1->hash (), key1_open->hash () } },
		{ gen_send2->hash (), { gen_receive->hash () } },
		{ key2_open->hash (), { gen_send2->hash () } },
		{ key2_send1->hash (), { key2_open->hash () } },
		{ key3_open->hash (), { key2_send1->hash () } },
		{ key2_send2->hash (), { key2_send1->hash () } },
		{ key1_receive->hash (), { key1_send1->hash (), key2_send2->hash () } },
		{ key1_send2->hash (), { key1_send1->hash () } },
		{ key3_receive->hash (), { key3_open->hash (), key1_send2->hash () } },
		{ key3_epoch->hash (), { key3_receive->hash () } },
	};
	ASSERT_EQ (node.ledger.block_count () - 2, dependency_graph.size ());

	// Start an election for the first block of the dependency graph, and ensure all blocks are eventually confirmed
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	node.start_election (gen_send1);

	ASSERT_NO_ERROR (system.poll_until_true (15s, [&] {
		// Not many blocks should be active simultaneously
		EXPECT_LT (node.active.size (), 6);

		// Ensure that active blocks have their ancestors confirmed
		auto error = std::any_of (dependency_graph.cbegin (), dependency_graph.cend (), [&] (auto entry) {
			if (node.vote_router.active (entry.first))
			{
				for (auto ancestor : entry.second)
				{
					if (!node.block_confirmed (ancestor))
					{
						return true;
					}
				}
			}
			return false;
		});

		EXPECT_FALSE (error);
		return error || node.ledger.cemented_count () == node.ledger.block_count ();
	}));
	ASSERT_EQ (node.ledger.cemented_count (), node.ledger.block_count ());
	ASSERT_TIMELY (5s, node.active.empty ());
}

// Confirm a complex dependency graph. Uses frontiers confirmation which will fail to
// confirm a frontier optimistically then fallback to pessimistic confirmation.
TEST (node, dependency_graph_frontier)
{
	lumex::test::system system;
	lumex::node_config config;
	config.peering_port = system.get_available_port ();
	config.backlog_scan->enable = false;
	auto & node1 = *system.add_node (config);
	config.peering_port = system.get_available_port ();
	config.backlog_scan->enable = true;
	auto & node2 = *system.add_node (config);

	lumex::state_block_builder builder;
	lumex::keypair key1, key2, key3;

	// Send to key1
	auto gen_send1 = builder.make_block ()
					 .account (lumex::dev::genesis_key.pub)
					 .previous (lumex::dev::genesis->hash ())
					 .representative (lumex::dev::genesis_key.pub)
					 .link (key1.pub)
					 .balance (lumex::dev::constants.genesis_amount - 1)
					 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					 .work (*system.work.generate (lumex::dev::genesis->hash ()))
					 .build ();
	// Receive from genesis
	auto key1_open = builder.make_block ()
					 .account (key1.pub)
					 .previous (0)
					 .representative (key1.pub)
					 .link (gen_send1->hash ())
					 .balance (1)
					 .sign (key1.prv, key1.pub)
					 .work (*system.work.generate (key1.pub))
					 .build ();
	// Send to genesis
	auto key1_send1 = builder.make_block ()
					  .account (key1.pub)
					  .previous (key1_open->hash ())
					  .representative (key1.pub)
					  .link (lumex::dev::genesis_key.pub)
					  .balance (0)
					  .sign (key1.prv, key1.pub)
					  .work (*system.work.generate (key1_open->hash ()))
					  .build ();
	// Receive from key1
	auto gen_receive = builder.make_block ()
					   .from (*gen_send1)
					   .previous (gen_send1->hash ())
					   .link (key1_send1->hash ())
					   .balance (lumex::dev::constants.genesis_amount)
					   .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					   .work (*system.work.generate (gen_send1->hash ()))
					   .build ();
	// Send to key2
	auto gen_send2 = builder.make_block ()
					 .from (*gen_receive)
					 .previous (gen_receive->hash ())
					 .link (key2.pub)
					 .balance (gen_receive->balance_field ().value ().number () - 2)
					 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					 .work (*system.work.generate (gen_receive->hash ()))
					 .build ();
	// Receive from genesis
	auto key2_open = builder.make_block ()
					 .account (key2.pub)
					 .previous (0)
					 .representative (key2.pub)
					 .link (gen_send2->hash ())
					 .balance (2)
					 .sign (key2.prv, key2.pub)
					 .work (*system.work.generate (key2.pub))
					 .build ();
	// Send to key3
	auto key2_send1 = builder.make_block ()
					  .account (key2.pub)
					  .previous (key2_open->hash ())
					  .representative (key2.pub)
					  .link (key3.pub)
					  .balance (1)
					  .sign (key2.prv, key2.pub)
					  .work (*system.work.generate (key2_open->hash ()))
					  .build ();
	// Receive from key2
	auto key3_open = builder.make_block ()
					 .account (key3.pub)
					 .previous (0)
					 .representative (key3.pub)
					 .link (key2_send1->hash ())
					 .balance (1)
					 .sign (key3.prv, key3.pub)
					 .work (*system.work.generate (key3.pub))
					 .build ();
	// Send to key1
	auto key2_send2 = builder.make_block ()
					  .from (*key2_send1)
					  .previous (key2_send1->hash ())
					  .link (key1.pub)
					  .balance (key2_send1->balance_field ().value ().number () - 1)
					  .sign (key2.prv, key2.pub)
					  .work (*system.work.generate (key2_send1->hash ()))
					  .build ();
	// Receive from key2
	auto key1_receive = builder.make_block ()
						.from (*key1_send1)
						.previous (key1_send1->hash ())
						.link (key2_send2->hash ())
						.balance (key1_send1->balance_field ().value ().number () + 1)
						.sign (key1.prv, key1.pub)
						.work (*system.work.generate (key1_send1->hash ()))
						.build ();
	// Send to key3
	auto key1_send2 = builder.make_block ()
					  .from (*key1_receive)
					  .previous (key1_receive->hash ())
					  .link (key3.pub)
					  .balance (key1_receive->balance_field ().value ().number () - 1)
					  .sign (key1.prv, key1.pub)
					  .work (*system.work.generate (key1_receive->hash ()))
					  .build ();
	// Receive from key1
	auto key3_receive = builder.make_block ()
						.from (*key3_open)
						.previous (key3_open->hash ())
						.link (key1_send2->hash ())
						.balance (key3_open->balance_field ().value ().number () + 1)
						.sign (key3.prv, key3.pub)
						.work (*system.work.generate (key3_open->hash ()))
						.build ();
	// Upgrade key3
	auto key3_epoch = builder.make_block ()
					  .from (*key3_receive)
					  .previous (key3_receive->hash ())
					  .link (node1.ledger.epoch_link (lumex::epoch::epoch_1))
					  .balance (key3_receive->balance_field ().value ())
					  .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					  .work (*system.work.generate (key3_receive->hash ()))
					  .build ();

	for (auto const & node : system.nodes)
	{
		auto transaction = node->ledger.tx_begin_write ();
		ASSERT_EQ (lumex::block_status::progress, node->ledger.process (transaction, gen_send1));
		ASSERT_EQ (lumex::block_status::progress, node->ledger.process (transaction, key1_open));
		ASSERT_EQ (lumex::block_status::progress, node->ledger.process (transaction, key1_send1));
		ASSERT_EQ (lumex::block_status::progress, node->ledger.process (transaction, gen_receive));
		ASSERT_EQ (lumex::block_status::progress, node->ledger.process (transaction, gen_send2));
		ASSERT_EQ (lumex::block_status::progress, node->ledger.process (transaction, key2_open));
		ASSERT_EQ (lumex::block_status::progress, node->ledger.process (transaction, key2_send1));
		ASSERT_EQ (lumex::block_status::progress, node->ledger.process (transaction, key3_open));
		ASSERT_EQ (lumex::block_status::progress, node->ledger.process (transaction, key2_send2));
		ASSERT_EQ (lumex::block_status::progress, node->ledger.process (transaction, key1_receive));
		ASSERT_EQ (lumex::block_status::progress, node->ledger.process (transaction, key1_send2));
		ASSERT_EQ (lumex::block_status::progress, node->ledger.process (transaction, key3_receive));
		ASSERT_EQ (lumex::block_status::progress, node->ledger.process (transaction, key3_epoch));
	}

	// node1 can vote, but only on the first block
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);

	ASSERT_TIMELY (10s, node2.active.active (gen_send1->qualified_root ()));
	node1.start_election (gen_send1);

	ASSERT_TIMELY_EQ (15s, node1.ledger.cemented_count (), node1.ledger.block_count ());
	ASSERT_TIMELY_EQ (15s, node2.ledger.cemented_count (), node2.ledger.block_count ());
}

TEST (node, deferred_dependent_elections)
{
	lumex::test::system system;
	lumex::node_config node_config_1;
	node_config_1.peering_port = system.get_available_port ();
	node_config_1.backlog_scan->enable = false;
	lumex::node_config node_config_2;
	node_config_2.peering_port = system.get_available_port ();
	node_config_2.backlog_scan->enable = false;
	lumex::node_flags flags;
	flags.disable_request_loop = true;
	auto & node = *system.add_node (node_config_1, flags);
	auto & node2 = *system.add_node (node_config_2, flags); // node2 will be used to ensure all blocks are being propagated

	lumex::state_block_builder builder;
	lumex::keypair key;
	auto send1 = builder.make_block ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (lumex::dev::genesis->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (lumex::dev::constants.genesis_amount - 1)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();
	auto open = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.link (send1->hash ())
				.balance (1)
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build ();
	auto send2 = builder.make_block ()
				 .from (*send1)
				 .previous (send1->hash ())
				 .balance (send1->balance_field ().value ().number () - 1)
				 .link (key.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	auto receive = builder.make_block ()
				   .from (*open)
				   .previous (open->hash ())
				   .link (send2->hash ())
				   .balance (2)
				   .sign (key.prv, key.pub)
				   .work (*system.work.generate (open->hash ()))
				   .build ();
	auto fork = builder.make_block ()
				.from (*receive)
				.representative (lumex::dev::genesis_key.pub) // was key.pub
				.sign (key.prv, key.pub)
				.build ();

	lumex::test::process (node, { send1 });
	auto election_send1 = lumex::test::start_election (system, node, send1->hash ());
	ASSERT_NE (nullptr, election_send1);

	// Should process and republish but not start an election for any dependent blocks
	lumex::test::process (node, { open, send2 });
	ASSERT_TIMELY (5s, node.block (open->hash ()));
	ASSERT_TIMELY (5s, node.block (send2->hash ()));
	ASSERT_NEVER (0.5s, node.active.active (open->qualified_root ()) || node.active.active (send2->qualified_root ()));
	ASSERT_TIMELY (5s, node2.block (open->hash ()));
	ASSERT_TIMELY (5s, node2.block (send2->hash ()));

	// Re-processing older blocks with updated work also does not start an election
	node.work_generate_blocking (*open, lumex::dev::network_params.work.difficulty (*open) + 1);
	node.process_local (open);
	ASSERT_NEVER (0.5s, node.active.active (open->qualified_root ()));

	// It is however possible to manually start an election from elsewhere
	ASSERT_TRUE (lumex::test::start_election (system, node, open->hash ()));
	node.active.erase (*open);
	ASSERT_FALSE (node.active.active (open->qualified_root ()));

	/// The election was dropped but it's still not possible to restart it
	node.work_generate_blocking (*open, lumex::dev::network_params.work.difficulty (*open) + 1);
	ASSERT_FALSE (node.active.active (open->qualified_root ()));
	node.process_local (open);
	ASSERT_NEVER (0.5s, node.active.active (open->qualified_root ()));

	// Drop both elections
	node.active.erase (*open);
	ASSERT_FALSE (node.active.active (open->qualified_root ()));
	node.active.erase (*send2);
	ASSERT_FALSE (node.active.active (send2->qualified_root ()));

	// Confirming send1 will automatically start elections for the dependents
	election_send1->force_confirm ();
	ASSERT_TIMELY (5s, node.block_confirmed (send1->hash ()));
	ASSERT_TIMELY (5s, node.active.active (open->qualified_root ()));
	ASSERT_TIMELY (5s, node.active.active (send2->qualified_root ()));
	auto election_open = node.active.election (open->qualified_root ());
	ASSERT_NE (nullptr, election_open);
	auto election_send2 = node.active.election (send2->qualified_root ());
	ASSERT_NE (nullptr, election_open);

	// Confirm one of the dependencies of the receive but not the other, to ensure both have to be confirmed to start an election on processing
	ASSERT_EQ (lumex::block_status::progress, node.process (receive));
	ASSERT_FALSE (node.active.active (receive->qualified_root ()));
	election_open->force_confirm ();
	ASSERT_TIMELY (5s, node.block_confirmed (open->hash ()));
	ASSERT_FALSE (node.ledger.dependencies_cemented (node.ledger.tx_begin_read (), *receive));
	ASSERT_NEVER (0.5s, node.active.active (receive->qualified_root ()));
	ASSERT_FALSE (node.ledger.rollback (node.ledger.tx_begin_write (), receive->hash ()));
	ASSERT_FALSE (node.block (receive->hash ()));
	node.process_local (receive);
	ASSERT_TIMELY (5s, node.block (receive->hash ()));
	ASSERT_NEVER (0.5s, node.active.active (receive->qualified_root ()));

	// Processing a fork will also not start an election
	ASSERT_EQ (lumex::block_status::fork, node.process (fork));
	node.process_local (fork);
	ASSERT_NEVER (0.5s, node.active.active (receive->qualified_root ()));

	// Confirming the other dependency allows starting an election from a fork
	election_send2->force_confirm ();
	ASSERT_TIMELY (5s, node.block_confirmed (send2->hash ()));
	ASSERT_TIMELY (5s, node.active.active (receive->qualified_root ()));
}

// Test that a node configured with `enable_pruning` and `max_pruning_age = 1s` will automatically
// prune old confirmed blocks without explicitly saying `node.ledger_pruning` in the unit test
TEST (node, pruning_automatic)
{
	lumex::test::system system{};

	lumex::node_config node_config;
	node_config.peering_port = system.get_available_port ();
	// TODO: remove after allowing pruned voting
	node_config.enable_voting = false;
	node_config.max_pruning_age = std::chrono::seconds (1);

	lumex::node_flags node_flags{};
	node_flags.enable_pruning = true;
	node_flags.disable_topo_index = true; // Topo index is incompatible with pruning

	auto & node1 = *system.add_node (node_config, node_flags);
	lumex::keypair key1{};
	lumex::send_block_builder builder{};
	auto latest_hash = lumex::dev::genesis->hash ();

	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();
	node1.process_active (send1);

	latest_hash = send1->hash ();
	auto send2 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (0)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();
	node1.process_active (send2);
	ASSERT_TIMELY (5s, node1.block (send2->hash ()) != nullptr);

	// Force-confirm both blocks
	node1.cementing_set.add (send1->hash ());
	ASSERT_TIMELY (5s, node1.block_confirmed (send1->hash ()));
	node1.cementing_set.add (send2->hash ());
	ASSERT_TIMELY (5s, node1.block_confirmed (send2->hash ()));

	// Check pruning result
	ASSERT_EQ (3, node1.ledger.block_count ());
	ASSERT_TIMELY_EQ (5s, node1.ledger.pruned_count (), 1);
	ASSERT_TIMELY_EQ (5s, node1.store.pruned.count (node1.store.tx_begin_read ()), 1);
	ASSERT_EQ (1, node1.ledger.pruned_count ());
	ASSERT_EQ (3, node1.ledger.block_count ());

	ASSERT_TRUE (lumex::test::block_or_pruned_all_exists (node1, { lumex::dev::genesis, send1, send2 }));
}

TEST (node, pruning_age)
{
	lumex::test::system system{};

	lumex::node_config node_config;
	node_config.peering_port = system.get_available_port ();
	// TODO: remove after allowing pruned voting
	node_config.enable_voting = false;

	lumex::node_flags node_flags{};
	node_flags.enable_pruning = true;
	node_flags.disable_topo_index = true; // Topo index is incompatible with pruning

	auto & node1 = *system.add_node (node_config, node_flags);
	lumex::keypair key1{};
	lumex::send_block_builder builder{};
	auto latest_hash = lumex::dev::genesis->hash ();

	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();
	node1.process_active (send1);

	latest_hash = send1->hash ();
	auto send2 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (0)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();
	node1.process_active (send2);

	// Force-confirm both blocks
	node1.cementing_set.add (send1->hash ());
	ASSERT_TIMELY (5s, node1.block_confirmed (send1->hash ()));
	node1.cementing_set.add (send2->hash ());
	ASSERT_TIMELY (5s, node1.block_confirmed (send2->hash ()));

	// Three blocks in total, nothing pruned yet
	ASSERT_EQ (0, node1.ledger.pruned_count ());
	ASSERT_EQ (3, node1.ledger.block_count ());

	// Pruning with default age 1 day
	node1.pruning.ledger_pruning (1, true);
	ASSERT_EQ (0, node1.ledger.pruned_count ());
	ASSERT_EQ (3, node1.ledger.block_count ());

	// Pruning with max age 0
	node1.config.max_pruning_age = std::chrono::seconds{ 0 };
	node1.pruning.ledger_pruning (1, true);
	ASSERT_EQ (1, node1.ledger.pruned_count ());
	ASSERT_EQ (3, node1.ledger.block_count ());

	ASSERT_TRUE (lumex::test::block_or_pruned_all_exists (node1, { lumex::dev::genesis, send1, send2 }));
}

// Test that a node configured with `enable_pruning` will
// prune DEEP-enough confirmed blocks by explicitly saying `node.ledger_pruning` in the unit test
TEST (node, pruning_depth)
{
	lumex::test::system system{};

	lumex::node_config node_config;
	node_config.peering_port = system.get_available_port ();
	// TODO: remove after allowing pruned voting
	node_config.enable_voting = false;

	lumex::node_flags node_flags{};
	node_flags.enable_pruning = true;
	node_flags.disable_topo_index = true; // Topo index is incompatible with pruning

	auto & node1 = *system.add_node (node_config, node_flags);
	lumex::keypair key1{};
	lumex::send_block_builder builder{};
	auto latest_hash = lumex::dev::genesis->hash ();

	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();
	node1.process_active (send1);

	latest_hash = send1->hash ();
	auto send2 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (0)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();
	node1.process_active (send2);

	// Force-confirm both blocks
	node1.cementing_set.add (send1->hash ());
	ASSERT_TIMELY (5s, node1.block_confirmed (send1->hash ()));
	node1.cementing_set.add (send2->hash ());
	ASSERT_TIMELY (5s, node1.block_confirmed (send2->hash ()));

	// Three blocks in total, nothing pruned yet
	ASSERT_EQ (0, node1.ledger.pruned_count ());
	ASSERT_EQ (3, node1.ledger.block_count ());

	// Pruning with default depth (unlimited)
	node1.pruning.ledger_pruning (1, true);
	ASSERT_EQ (0, node1.ledger.pruned_count ());
	ASSERT_EQ (3, node1.ledger.block_count ());

	// Pruning with max depth 1
	node1.config.max_pruning_depth = 1;
	node1.pruning.ledger_pruning (1, true);
	ASSERT_EQ (1, node1.ledger.pruned_count ());
	ASSERT_EQ (3, node1.ledger.block_count ());

	ASSERT_TRUE (lumex::test::block_or_pruned_all_exists (node1, { lumex::dev::genesis, send1, send2 }));
}

TEST (node_config, node_id_private_key_persistence)
{
	lumex::test::system system;

	// create the directory and the file
	auto path = lumex::unique_path ();
	ASSERT_TRUE (std::filesystem::exists (path));
	auto priv_key_filename = path / "node_id_private.key";

	// check that the key generated is random when the key does not exist
	lumex::keypair kp1 = lumex::load_or_create_node_id (path);
	std::filesystem::remove (priv_key_filename);
	lumex::keypair kp2 = lumex::load_or_create_node_id (path);
	ASSERT_NE (kp1.prv, kp2.prv);

	// check that the key persists
	lumex::keypair kp3 = lumex::load_or_create_node_id (path);
	ASSERT_EQ (kp2.prv, kp3.prv);

	// write the key file manually and check that right key is loaded
	std::ofstream ofs (priv_key_filename.string (), std::ofstream::out | std::ofstream::trunc);
	ofs << "3F28D035B8AA75EA53DF753BFD065CF6138E742971B2C99B84FD8FE328FED2D9" << std::flush;
	ofs.close ();
	lumex::keypair kp4 = lumex::load_or_create_node_id (path);
	ASSERT_EQ (kp4.prv, lumex::keypair ("3F28D035B8AA75EA53DF753BFD065CF6138E742971B2C99B84FD8FE328FED2D9").prv);
}

TEST (node, port_mapping)
{
	lumex::test::system system;
	auto node = system.add_node ();
	node->port_mapping.refresh_devices ();
}

TEST (node, process_local_overflow)
{
	lumex::test::system system;
	auto config = system.default_config ();
	config.block_processor->max_system_queue = 0;
	auto & node = *system.add_node (config);

	lumex::keypair key1;
	lumex::send_block_builder builder;
	auto latest_hash = lumex::dev::genesis->hash ();
	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();

	auto result = node.process_local (send1);
	ASSERT_FALSE (result);
}

TEST (node, local_block_broadcast)
{
	lumex::test::system system;

	// Disable active elections to prevent the block from being broadcasted by the election
	auto node_config = system.default_config ();
	node_config.priority_scheduler->enable = false;
	node_config.hinted_scheduler->enable = false;
	node_config.optimistic_scheduler->enable = false;
	node_config.local_block_broadcaster->rebroadcast_interval = 1s;
	auto & node1 = *system.add_node (node_config);
	auto & node2 = *system.make_disconnected_node ();

	lumex::keypair key1;
	lumex::send_block_builder builder;
	auto latest_hash = lumex::dev::genesis->hash ();
	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .destination (key1.pub)
				 .balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();

	auto result = node1.process_local (send1);
	ASSERT_TRUE (result);
	ASSERT_NEVER (500ms, node1.active.active (send1->qualified_root ()));

	// Wait until a broadcast is attempted
	ASSERT_TIMELY_EQ (5s, node1.local_block_broadcaster.size (), 1);
	ASSERT_TIMELY (5s, node1.stats.count (lumex::stat::type::local_block_broadcaster, lumex::stat::detail::broadcast) >= 1);

	// The other node should not have received the block
	ASSERT_NEVER (500ms, node2.block (send1->hash ()));

	// Connect the nodes and check that the block is propagated
	node1.network.merge_peer (node2.network.endpoint ());
	ASSERT_TIMELY (5s, node1.network.find_node_id (node2.get_node_id ()));
	ASSERT_TIMELY (10s, node2.block (send1->hash ()));
}

TEST (node, container_info)
{
	lumex::test::system system;
	auto & node1 = *system.add_node ();
	auto & node2 = *system.add_node ();

	// Generate some random activity
	std::vector<lumex::account> accounts;
	auto dev_genesis_key = lumex::dev::genesis_key;
	system.wallet (0)->insert_adhoc (dev_genesis_key.prv);
	accounts.push_back (dev_genesis_key.pub);
	for (int n = 0; n < 10; ++n)
	{
		system.generate_activity (node1, accounts);
	}

	// This should just execute, sanitizers will catch any problems
	ASSERT_NO_THROW (node1.container_info ());
	ASSERT_NO_THROW (node2.container_info ());
}

TEST (node, bounded_backlog)
{
	lumex::test::system system;

	lumex::node_config node_config;
	node_config.max_backlog = 10;
	node_config.backlog_scan->enable = false;
	auto & node = *system.add_node (node_config);

	const int howmany_blocks = 64;
	const int howmany_chains = 16;

	auto chains = lumex::test::setup_chains (system, node, howmany_chains, howmany_blocks, lumex::dev::genesis_key, /* do not confirm */ false);

	node.backlog_scan.trigger ();

	ASSERT_TIMELY_EQ (20s, node.ledger.block_count (), 11); // 10 + genesis
}

// This test checks that a bootstrapping node can resolve a fork when a "poisoned" node
// attempts to feed it the incorrect side of a fork.
// The scenario involves:
// 1. A bootstrapping node (node_boot) - the node being tested
// 2. A poisoned node (node_poison) that has bootstrap serving enabled with an incorrect block
// 3. A representative node (node_rep) with enough voting weight but bootstrap serving disabled
// 4. A non-representative node (node_correct) that serves the correct side of the fork
TEST (node, bootstrap_poison)
{
	lumex::test::system system;

	// Create the representative node with bootstrap serving disabled
	lumex::node_config rep_config = system.default_config ();
	rep_config.bootstrap_server->enable = false; // Disable bootstrap serving
	rep_config.bootstrap->enable = false; // Disable bootstrap from the network
	// Disable schedulers
	rep_config.priority_scheduler->enable = false;
	rep_config.hinted_scheduler->enable = false;
	rep_config.optimistic_scheduler->enable = false;
	rep_config.backlog_scan->enable = false;
	auto & node_rep = *system.add_node (rep_config);

	// Create the poisoned node with bootstrap serving enabled
	lumex::node_config poison_config = system.default_config ();
	poison_config.bootstrap_server->enable = true; // Enable bootstrap serving
	poison_config.bootstrap->enable = false; // Disable bootstrap from the network
	// Disable schedulers
	poison_config.priority_scheduler->enable = false;
	poison_config.hinted_scheduler->enable = false;
	poison_config.optimistic_scheduler->enable = false;
	poison_config.backlog_scan->enable = false;
	auto & node_poison = *system.add_node (poison_config);

	// Representative node needs to hold the genesis key to have voting weight
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);

	// Create keys for our test accounts
	lumex::keypair key1;

	// Create and process blocks on representative node (the correct chain)
	lumex::block_builder builder;

	// First send from genesis to key1
	auto send1 = builder.send ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();

	ASSERT_EQ (lumex::block_status::progress, node_rep.process (send1));
	ASSERT_EQ (lumex::block_status::progress, node_poison.process (send1));

	// Valid open block for key1 (correct version)
	auto open_correct = builder.open ()
						.source (send1->hash ())
						.representative (key1.pub)
						.account (key1.pub)
						.sign (key1.prv, key1.pub)
						.work (*system.work.generate (key1.pub))
						.build ();

	// Fork of the open block (incorrect version) - using a different representative
	lumex::keypair bad_rep;
	auto open_fork = builder.open ()
					 .source (send1->hash ())
					 .representative (bad_rep.pub) // Different representative
					 .account (key1.pub)
					 .sign (key1.prv, key1.pub)
					 .work (*system.work.generate (key1.pub))
					 .build ();

	// Process the correct open block on the representative node
	ASSERT_EQ (lumex::block_status::progress, node_rep.process (open_correct));

	// Process the forked open block on the poisoned node
	ASSERT_EQ (lumex::block_status::progress, node_poison.process (open_fork));

	// Confirm the correct block on the representative node
	lumex::test::confirm (node_rep, { open_correct });

	// Now create a bootstrapping node that will try to sync from both nodes
	lumex::node_config node_config = system.default_config ();
	node_config.bootstrap->account_sets.cooldown = 100ms; // Short cooldown between requests to speed up the test
	node_config.bootstrap->request_timeout = 250ms;
	node_config.bootstrap->frontier_rate_limit = 100;
	// Disable schedulers
	node_config.priority_scheduler->enable = false;
	node_config.hinted_scheduler->enable = false;
	node_config.optimistic_scheduler->enable = false;
	node_config.backlog_scan->enable = false;
	lumex::node_flags node_flags;
	auto & node = *system.add_node (node_config, node_flags);
	ASSERT_EQ (node.network.size (), 2);

	std::cout << "Main node: " << node.identifier () << std::endl;
	std::cout << "Waiting for: " << open_fork->hash ().to_string () << std::endl;

	// The node should initially get the incorrect block from the poisoned node
	ASSERT_TIMELY (15s, node.block (open_fork->hash ()) != nullptr);
	ASSERT_NEVER (1s, node.stats.count (lumex::stat::type::ledger, lumex::stat::detail::fork) > 0);

	// Create another non-rep node that will serve the correct side of the fork
	lumex::node_config correct_config = system.default_config ();
	correct_config.bootstrap_server->enable = true; // Enable bootstrap serving
	correct_config.bootstrap->enable = false; // Disable bootstrap from the network
	correct_config.priority_scheduler->enable = false;
	correct_config.hinted_scheduler->enable = false;
	correct_config.optimistic_scheduler->enable = false;
	auto & node_correct = *system.add_node (correct_config);
	ASSERT_EQ (node.network.size (), 3);

	// Process the correct open block on the non-representative node
	ASSERT_EQ (lumex::block_status::progress, node_correct.process (send1));
	ASSERT_EQ (lumex::block_status::progress, node_correct.process (open_correct));

	// The node should at some point notice that there is a forked block
	ASSERT_TIMELY (15s, node.stats.count (lumex::stat::type::ledger, lumex::stat::detail::fork) > 0);

	// Should no longer be needed, fork should be cached
	node_correct.stop ();

	// We need an election active to force the correct side of the fork
	ASSERT_TRUE (lumex::test::start_election (system, node, open_fork->hash ()));

	// Wait for the node to resolve the fork conflict
	ASSERT_TIMELY (15s, node.block (open_correct->hash ()) != nullptr);

	// Verify that the node got the correct block and not the fork
	ASSERT_NE (nullptr, node.block (open_correct->hash ()));
	ASSERT_EQ (nullptr, node.block (open_fork->hash ()));

	// Verify the account information on the bootstrap node is correct
	lumex::account_info account_info;
	ASSERT_FALSE (node.store.account.get (node.store.tx_begin_read (), key1.pub, account_info));
	ASSERT_EQ (account_info.head, open_correct->hash ());
	ASSERT_EQ (account_info.representative, key1.pub); // Correct representative
}

TEST (node, super_rebroadcaster)
{
	lumex::test::system system;

	// Node 1: Super rebroadcaster mode
	lumex::node_config config = system.default_config ();
	config.bootstrap->enable = false;
	config.local_block_broadcaster->enable = false;
	lumex::node_flags flags;
	flags.super_rebroadcaster = true;
	auto & node1 = *system.add_node (config, flags);

	// Nodes 2, 3, 4: Normal peer nodes
	lumex::node_config peer_config = system.default_config ();
	peer_config.block_rebroadcaster->enable = false;
	peer_config.vote_rebroadcaster->enable = false;
	peer_config.local_block_broadcaster->enable = false;
	auto & node2 = *system.add_node (peer_config);
	auto & node3 = *system.add_node (peer_config);
	auto & node4 = *system.add_node (peer_config);

	// Verify all nodes connected
	ASSERT_TIMELY_EQ (5s, node1.network.size (), 3);

	// Create a block
	auto send = lumex::state_block_builder ()
				.account (lumex::dev::genesis_key.pub)
				.previous (lumex::dev::genesis->hash ())
				.representative (lumex::dev::genesis_key.pub)
				.balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				.link (lumex::dev::genesis_key.pub)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*system.work.generate (lumex::dev::genesis->hash ()))
				.build ();

	// Process block as if received from the live network (triggers rebroadcasting)
	node1.process_active (send);

	// Verify block rebroadcasting uses super mode
	ASSERT_TIMELY (5s, node1.stats.count (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::broadcast_super) >= 1);
	ASSERT_EQ (0, node1.stats.count (lumex::stat::type::block_rebroadcaster, lumex::stat::detail::broadcast));

	// Verify ALL peers received the block (super mode floods to all)
	ASSERT_TIMELY (5s, node2.block (send->hash ()));
	ASSERT_TIMELY (5s, node3.block (send->hash ()));
	ASSERT_TIMELY (5s, node4.block (send->hash ()));

	// Verify vote rebroadcasting uses super mode
	// Create a final vote for the block (final vote will confirm the block)
	auto vote = lumex::test::make_final_vote (lumex::dev::genesis_key, { send });

	// Process vote as if received from the live network (triggers rebroadcasting)
	node1.process_active (vote);

	// Verify super mode stat is used for vote rebroadcasting
	ASSERT_TIMELY (5s, node1.stats.count (lumex::stat::type::vote_rebroadcaster, lumex::stat::detail::broadcast_super) >= 1);
	ASSERT_EQ (0, node1.stats.count (lumex::stat::type::vote_rebroadcaster, lumex::stat::detail::broadcast));

	// Verify peers received votes with rebroadcasted flag set
	ASSERT_TIMELY (5s, node2.stats.count (lumex::stat::type::vote_processor_source, lumex::stat::detail::rebroadcast) >= 1);
	ASSERT_TIMELY (5s, node3.stats.count (lumex::stat::type::vote_processor_source, lumex::stat::detail::rebroadcast) >= 1);
	ASSERT_TIMELY (5s, node4.stats.count (lumex::stat::type::vote_processor_source, lumex::stat::detail::rebroadcast) >= 1);

	// Verify all peers confirmed the block (final vote triggers confirmation)
	ASSERT_TIMELY (5s, node2.block_confirmed (send->hash ()));
	ASSERT_TIMELY (5s, node3.block_confirmed (send->hash ()));
	ASSERT_TIMELY (5s, node4.block_confirmed (send->hash ()));
}

// Tests that when two nodes try to bind to the same port, the second node fails gracefully
TEST (node, port_already_in_use)
{
	lumex::test::system system;

	// Start first node on the port
	auto node1 = system.add_node ();
	ASSERT_NE (0, node1->network.port);

	// Try to start second node on the same port
	lumex::node_config config2;
	config2.peering_port = node1->network.port;

	// Create node in a scope so it gets destroyed if start() throws
	auto node2 = std::make_shared<lumex::node> (lumex::unique_path (), config2, system.work, lumex::node_flags{});

	// This should throw boost::system::system_error indicating port is already in use
	ASSERT_THROW (node2->start (), boost::system::system_error);

	// Exit gracefully
	node2->stop ();
}
