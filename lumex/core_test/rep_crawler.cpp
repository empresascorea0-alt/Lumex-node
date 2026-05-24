#include <lumex/lib/blockbuilders.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/config.hpp>
#include <lumex/lib/files.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/vote.hpp>
#include <lumex/messages/confirm.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/online_reps.hpp>
#include <lumex/node/repcrawler.hpp>
#include <lumex/node/transport/fake.hpp>
#include <lumex/node/transport/inproc.hpp>
#include <lumex/node/wallet.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/test_common/chains.hpp>
#include <lumex/test_common/network.hpp>
#include <lumex/test_common/system.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <numeric>

using namespace std::chrono_literals;

// Test that nodes can track nodes that have rep weight for priority broadcasting
TEST (rep_crawler, rep_list)
{
	lumex::test::system system;
	auto & node1 = *system.add_node ();
	auto & node2 = *system.add_node ();
	ASSERT_EQ (0, node2.rep_crawler.representative_count ());
	// Node #1 has a rep
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	ASSERT_TIMELY_EQ (5s, node2.rep_crawler.representative_count (), 1);
	auto reps = node2.rep_crawler.representatives ();
	ASSERT_EQ (1, reps.size ());
	ASSERT_EQ (lumex::dev::genesis_key.pub, reps[0].account);
}

TEST (rep_crawler, rep_weight)
{
	lumex::test::system system;
	auto & node = *system.add_node ();
	auto & node1 = *system.add_node ();
	auto & node2 = *system.add_node ();
	auto & node3 = *system.add_node ();
	lumex::keypair keypair1;
	lumex::keypair keypair2;
	lumex::block_builder builder;
	auto const amount_pr = node.minimum_principal_weight () + 100;
	auto const amount_not_pr = node.minimum_principal_weight () - 100;
	std::shared_ptr<lumex::block> block1 = builder
										  .state ()
										  .account (lumex::dev::genesis_key.pub)
										  .previous (lumex::dev::genesis->hash ())
										  .representative (lumex::dev::genesis_key.pub)
										  .balance (lumex::dev::constants.genesis_amount - amount_not_pr)
										  .link (keypair1.pub)
										  .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
										  .work (*system.work.generate (lumex::dev::genesis->hash ()))
										  .build ();
	std::shared_ptr<lumex::block> block2 = builder
										  .state ()
										  .account (keypair1.pub)
										  .previous (0)
										  .representative (keypair1.pub)
										  .balance (amount_not_pr)
										  .link (block1->hash ())
										  .sign (keypair1.prv, keypair1.pub)
										  .work (*system.work.generate (keypair1.pub))
										  .build ();
	std::shared_ptr<lumex::block> block3 = builder
										  .state ()
										  .account (lumex::dev::genesis_key.pub)
										  .previous (block1->hash ())
										  .representative (lumex::dev::genesis_key.pub)
										  .balance (lumex::dev::constants.genesis_amount - amount_not_pr - amount_pr)
										  .link (keypair2.pub)
										  .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
										  .work (*system.work.generate (block1->hash ()))
										  .build ();
	std::shared_ptr<lumex::block> block4 = builder
										  .state ()
										  .account (keypair2.pub)
										  .previous (0)
										  .representative (keypair2.pub)
										  .balance (amount_pr)
										  .link (block3->hash ())
										  .sign (keypair2.prv, keypair2.pub)
										  .work (*system.work.generate (keypair2.pub))
										  .build ();
	ASSERT_TRUE (lumex::test::process (node, { block1, block2, block3, block4 }));
	ASSERT_TRUE (lumex::test::process (node1, { block1, block2, block3, block4 }));
	ASSERT_TRUE (lumex::test::process (node2, { block1, block2, block3, block4 }));
	ASSERT_TRUE (lumex::test::process (node3, { block1, block2, block3, block4 }));
	ASSERT_TRUE (node.rep_crawler.representatives (1).empty ());

	ASSERT_TIMELY (5s, node.network.size () == 3);
	auto channel1 = node.network.find_node_id (node1.get_node_id ());
	auto channel2 = node.network.find_node_id (node2.get_node_id ());
	auto channel3 = node.network.find_node_id (node3.get_node_id ());
	ASSERT_NE (nullptr, channel1);
	ASSERT_NE (nullptr, channel2);
	ASSERT_NE (nullptr, channel3);

	auto vote0 = std::make_shared<lumex::vote> (lumex::dev::genesis_key.pub, lumex::dev::genesis_key.prv, 0, 0, std::vector<lumex::block_hash>{ lumex::dev::genesis->hash () });
	auto vote1 = std::make_shared<lumex::vote> (keypair1.pub, keypair1.prv, 0, 0, std::vector<lumex::block_hash>{ lumex::dev::genesis->hash () });
	auto vote2 = std::make_shared<lumex::vote> (keypair2.pub, keypair2.prv, 0, 0, std::vector<lumex::block_hash>{ lumex::dev::genesis->hash () });
	node.rep_crawler.force_process (vote0, channel1);
	node.rep_crawler.force_process (vote1, channel2);
	node.rep_crawler.force_process (vote2, channel3);
	ASSERT_TIMELY_EQ (5s, node.rep_crawler.representative_count (), 2);
	// Make sure we get the rep with the most weight first
	auto reps = node.rep_crawler.representatives (1);
	ASSERT_EQ (1, reps.size ());
	ASSERT_EQ (node.balance (lumex::dev::genesis_key.pub), node.ledger.weight (reps[0].account));
	ASSERT_EQ (lumex::dev::genesis_key.pub, reps[0].account);
	ASSERT_EQ (channel1, reps[0].channel);
	ASSERT_TRUE (node.rep_crawler.is_pr (channel1));
	ASSERT_FALSE (node.rep_crawler.is_pr (channel2));
	ASSERT_TRUE (node.rep_crawler.is_pr (channel3));
}

// Test that rep_crawler removes unreachable reps from its search results.
// This test creates three principal representatives (rep1, rep2, genesis_rep) and
// one node for searching them (searching_node).
TEST (rep_crawler, rep_remove)
{
	lumex::test::system system;
	auto & searching_node = *system.add_node (); // will be used to find principal representatives
	lumex::keypair keys_rep1; // Principal representative 1
	lumex::keypair keys_rep2; // Principal representative 2
	lumex::block_builder builder;

	auto const rep_weight = lumex::test::minimum_principal_weight () * 2;

	// Send enough lumexs to Rep1 to make it a principal representative
	auto send_to_rep1 = builder
						.state ()
						.account (lumex::dev::genesis_key.pub)
						.previous (lumex::dev::genesis->hash ())
						.representative (lumex::dev::genesis_key.pub)
						.balance (lumex::dev::constants.genesis_amount - rep_weight)
						.link (keys_rep1.pub)
						.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
						.work (*system.work.generate (lumex::dev::genesis->hash ()))
						.build ();

	// Receive by Rep1
	auto receive_rep1 = builder
						.state ()
						.account (keys_rep1.pub)
						.previous (0)
						.representative (keys_rep1.pub)
						.balance (rep_weight)
						.link (send_to_rep1->hash ())
						.sign (keys_rep1.prv, keys_rep1.pub)
						.work (*system.work.generate (keys_rep1.pub))
						.build ();

	// Send enough lumexs to Rep2 to make it a principal representative
	auto send_to_rep2 = builder
						.state ()
						.account (lumex::dev::genesis_key.pub)
						.previous (send_to_rep1->hash ())
						.representative (lumex::dev::genesis_key.pub)
						.balance (lumex::dev::constants.genesis_amount - rep_weight * 2)
						.link (keys_rep2.pub)
						.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
						.work (*system.work.generate (send_to_rep1->hash ()))
						.build ();

	// Receive by Rep2
	auto receive_rep2 = builder
						.state ()
						.account (keys_rep2.pub)
						.previous (0)
						.representative (keys_rep2.pub)
						.balance (rep_weight)
						.link (send_to_rep2->hash ())
						.sign (keys_rep2.prv, keys_rep2.pub)
						.work (*system.work.generate (keys_rep2.pub))
						.build ();

	{
		auto transaction = searching_node.ledger.tx_begin_write ();
		ASSERT_EQ (lumex::block_status::progress, searching_node.ledger.process (transaction, send_to_rep1));
		ASSERT_EQ (lumex::block_status::progress, searching_node.ledger.process (transaction, receive_rep1));
		ASSERT_EQ (lumex::block_status::progress, searching_node.ledger.process (transaction, send_to_rep2));
		ASSERT_EQ (lumex::block_status::progress, searching_node.ledger.process (transaction, receive_rep2));
	}

	// Create channel for Rep1
	auto channel_rep1 (std::make_shared<lumex::transport::fake::channel> (searching_node));

	// Ensure Rep1 is found by the rep_crawler after receiving a vote from it
	auto vote_rep1 = std::make_shared<lumex::vote> (keys_rep1.pub, keys_rep1.prv, 0, 0, std::vector<lumex::block_hash>{ lumex::dev::genesis->hash () });
	ASSERT_LE (searching_node.minimum_principal_weight (), rep_weight);
	searching_node.rep_crawler.force_process (vote_rep1, channel_rep1);
	ASSERT_TIMELY_EQ (5s, searching_node.rep_crawler.representative_count (), 1);
	auto reps (searching_node.rep_crawler.representatives (1));
	ASSERT_EQ (1, reps.size ());
	ASSERT_LE (searching_node.minimum_principal_weight (), searching_node.ledger.weight (reps[0].account));
	ASSERT_EQ (keys_rep1.pub, reps[0].account);
	ASSERT_EQ (channel_rep1, reps[0].channel);

	// When rep1 disconnects then rep1 should not be found anymore
	channel_rep1->close ();
	ASSERT_TIMELY_EQ (5s, searching_node.rep_crawler.representative_count (), 0);

	// Add working node for genesis representative
	auto node_genesis_rep = system.add_node ([&] { lumex::node_config c; c.peering_port = system.get_available_port (); return c; }());
	system.wallet (1)->insert_adhoc (lumex::dev::genesis_key.prv);
	auto channel_genesis_rep (searching_node.network.find_node_id (node_genesis_rep->get_node_id ()));
	ASSERT_NE (nullptr, channel_genesis_rep);

	// genesis_rep should be found as principal representative after receiving a vote from it
	auto vote_genesis_rep = std::make_shared<lumex::vote> (lumex::dev::genesis_key.pub, lumex::dev::genesis_key.prv, 0, 0, std::vector<lumex::block_hash>{ lumex::dev::genesis->hash () });
	searching_node.rep_crawler.force_process (vote_genesis_rep, channel_genesis_rep);
	ASSERT_TIMELY_EQ (10s, searching_node.rep_crawler.representative_count (), 1);

	// Start a node for Rep2 and wait until it is connected
	auto node_rep2 (std::make_shared<lumex::node> (
	lumex::unique_path (), [&] { lumex::node_config c; c.peering_port = system.get_available_port (); return c; }(), system.work, lumex::node_flags{}));
	node_rep2->start ();
	searching_node.network.tcp_channels.start_tcp (node_rep2->network.endpoint ());
	std::shared_ptr<lumex::transport::channel> channel_rep2;
	ASSERT_TIMELY (10s, (channel_rep2 = searching_node.network.tcp_channels.find_node_id (node_rep2->get_node_id ())) != nullptr);

	// Rep2 should be found as a principal representative after receiving a vote from it
	auto vote_rep2 = std::make_shared<lumex::vote> (keys_rep2.pub, keys_rep2.prv, 0, 0, std::vector<lumex::block_hash>{ lumex::dev::genesis->hash () });
	searching_node.rep_crawler.force_process (vote_rep2, channel_rep2);
	ASSERT_TIMELY_EQ (10s, searching_node.rep_crawler.representative_count (), 2);

	// When Rep2 is stopped, it should not be found as principal representative anymore
	system.stop_node (*node_rep2);
	ASSERT_TIMELY_EQ (10s, searching_node.rep_crawler.representative_count (), 1);

	// Now only genesisRep should be found:
	reps = searching_node.rep_crawler.representatives (1);
	ASSERT_EQ (lumex::dev::genesis_key.pub, reps[0].account);
	ASSERT_TIMELY_EQ (5s, searching_node.network.size (), 1);
}

TEST (rep_crawler, rep_connection_close)
{
	lumex::test::system system;
	auto & node1 = *system.add_node ();
	auto & node2 = *system.add_node ();
	// Add working representative (node 2)
	system.wallet (1)->insert_adhoc (lumex::dev::genesis_key.prv);
	ASSERT_TIMELY_EQ (10s, node1.rep_crawler.representative_count (), 1);
	system.stop_node (node2);
	// Remove representative with closed channel
	ASSERT_TIMELY_EQ (10s, node1.rep_crawler.representative_count (), 0);
}

TEST (rep_crawler, rep_local)
{
	lumex::test::system system;
	auto & node = *system.add_node ();
	ASSERT_EQ (0, node.online_reps.online ());
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	ASSERT_TIMELY_EQ (5s, node.rep_crawler.representative_count (), 1);
	auto reps = node.rep_crawler.representatives ();
	ASSERT_EQ (1, reps.size ());
	ASSERT_EQ (lumex::dev::genesis_key.pub, reps[0].account);
}

// This test checks that if a block is in the recently_confirmed list then the repcrawler will not send a request for it.
// The behaviour of this test previously was the opposite, that the repcrawler eventually send out such a block and deleted the block
// from the recently confirmed list to try to make ammends for sending it, which is bad behaviour.
// In the long term, we should have a better way to check for reps and this test should become redundant
// DISABLED as behaviour changed, and we now only query confirmed blocks
TEST (rep_crawler, DISABLED_recently_confirmed)
{
	lumex::test::system system (1);
	auto & node1 (*system.nodes[0]);
	ASSERT_EQ (1, node1.ledger.block_count ());
	auto const block = lumex::dev::genesis;
	lumex::election_status status;
	status.winner = block;
	node1.active.recently_confirmed.put (block->qualified_root (), block->hash (), status);
	auto & node2 (*system.add_node ());
	system.wallet (1)->insert_adhoc (lumex::dev::genesis_key.prv);
	auto channel = node1.network.find_node_id (node2.get_node_id ());
	ASSERT_NE (nullptr, channel);
	node1.rep_crawler.query (channel); // this query should be dropped due to the recently_confirmed entry
	ASSERT_ALWAYS_EQ (0.5s, node1.rep_crawler.representative_count (), 0);
}

// Test that nodes can track PRs when multiple PRs are inside one node
TEST (rep_crawler, two_reps_one_node)
{
	lumex::test::system system;
	auto & node1 = *system.add_node ();
	auto & node2 = *system.add_node ();

	// create a second PR account
	lumex::keypair second_rep = lumex::test::setup_rep (system, node1, node1.balance (lumex::dev::genesis_key.pub) / 10);
	ASSERT_EQ (0, node2.rep_crawler.representative_count ());

	// enable the two PRs in node1
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	system.wallet (0)->insert_adhoc (second_rep.prv);

	ASSERT_TIMELY_EQ (15s, node2.rep_crawler.representative_count (), 2);
	auto reps = node2.rep_crawler.representatives ();
	ASSERT_EQ (2, reps.size ());

	// check that the reps are correct
	ASSERT_TRUE (lumex::dev::genesis_key.pub == reps[0].account || lumex::dev::genesis_key.pub == reps[1].account);
	ASSERT_TRUE (second_rep.pub == reps[0].account || second_rep.pub == reps[1].account);
}

TEST (rep_crawler, ignore_rebroadcasted)
{
	lumex::test::system system;
	auto & node1 = *system.add_node ();
	auto & node2 = *system.add_node ();

	auto channel1to2 = node1.network.find_node_id (node2.get_node_id ());
	ASSERT_NE (nullptr, channel1to2);

	node1.rep_crawler.force_query (lumex::dev::genesis->hash (), channel1to2);
	ASSERT_ALWAYS_EQ (100ms, node1.rep_crawler.representative_count (), 0);

	// Now we spam the vote for genesis, so it appears as a rebroadcasted vote
	auto vote = lumex::test::make_vote (lumex::dev::genesis_key, { lumex::dev::genesis->hash () }, 0);

	auto channel2to1 = node2.network.find_node_id (node1.get_node_id ());
	ASSERT_NE (nullptr, channel2to1);

	node1.rep_crawler.force_query (lumex::dev::genesis->hash (), channel1to2);

	auto tick = [&] () {
		lumex::messages::confirm_ack msg{ lumex::dev::network_params.network, vote, /* rebroadcasted */ true };
		channel2to1->send (msg, lumex::transport::traffic_type::test);
		return false;
	};

	ASSERT_NEVER (1s, tick () || node1.rep_crawler.representative_count () > 0);
}
