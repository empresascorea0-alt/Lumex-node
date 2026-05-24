#include <lumex/core_test/fakes/websocket_client.hpp>
#include <lumex/lib/blockbuilders.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/jsonconfig.hpp>
#include <lumex/lib/work_version.hpp>
#include <lumex/messages/messages.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/online_reps.hpp>
#include <lumex/node/scheduler/hinted.hpp>
#include <lumex/node/scheduler/optimistic.hpp>
#include <lumex/node/scheduler/priority.hpp>
#include <lumex/node/telemetry.hpp>
#include <lumex/node/transport/fake.hpp>
#include <lumex/node/vote_router.hpp>
#include <lumex/node/wallet.hpp>
#include <lumex/node/websocket.hpp>
#include <lumex/test_common/network.hpp>
#include <lumex/test_common/system.hpp>
#include <lumex/test_common/telemetry.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/format.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace std::chrono_literals;

// Tests clients subscribing multiple times or unsubscribing without a subscription
TEST (websocket, subscription_edge)
{
	lumex::test::system system;
	lumex::node_config config = system.default_config ();
	config.websocket_config->enabled = true;
	config.websocket_config->port = system.get_available_port ();
	auto node1 (system.add_node (config));

	ASSERT_EQ (0, node1->websocket.server->subscriber_count (lumex::websocket::topic::confirmation));

	auto task = ([config, &node1] () {
		fake_websocket_client client (node1->websocket.server->listening_port ());
		client.send_message (R"json({"action": "subscribe", "topic": "confirmation", "ack": true})json");
		client.await_ack ();
		EXPECT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::confirmation));
		client.send_message (R"json({"action": "subscribe", "topic": "confirmation", "ack": true})json");
		client.await_ack ();
		EXPECT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::confirmation));
		client.send_message (R"json({"action": "unsubscribe", "topic": "confirmation", "ack": true})json");
		client.await_ack ();
		EXPECT_EQ (0, node1->websocket.server->subscriber_count (lumex::websocket::topic::confirmation));
		client.send_message (R"json({"action": "unsubscribe", "topic": "confirmation", "ack": true})json");
		client.await_ack ();
		EXPECT_EQ (0, node1->websocket.server->subscriber_count (lumex::websocket::topic::confirmation));
	});
	auto future = std::async (std::launch::async, task);

	ASSERT_TIMELY_EQ (5s, future.wait_for (0s), std::future_status::ready);
}

// Subscribes to block confirmations, confirms a block and then awaits websocket notification
TEST (websocket, confirmation)
{
	lumex::test::system system;
	lumex::node_config config = system.default_config ();
	config.websocket_config->enabled = true;
	config.websocket_config->port = system.get_available_port ();
	auto node1 (system.add_node (config));

	std::atomic<bool> ack_ready{ false };
	std::atomic<bool> unsubscribed{ false };
	auto task = ([&ack_ready, &unsubscribed, config, &node1] () {
		fake_websocket_client client (node1->websocket.server->listening_port ());
		client.send_message (R"json({"action": "subscribe", "topic": "confirmation", "ack": true})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::confirmation));
		auto response = client.get_response ();
		EXPECT_TRUE (response);
		boost::property_tree::ptree event;
		std::stringstream stream;
		stream << response.value ();
		boost::property_tree::read_json (stream, event);
		EXPECT_EQ (event.get<std::string> ("topic"), "confirmation");
		client.send_message (R"json({"action": "unsubscribe", "topic": "confirmation", "ack": true})json");
		client.await_ack ();
		unsubscribed = true;
		EXPECT_FALSE (client.get_response (1s));
	});
	auto future = std::async (std::launch::async, task);

	ASSERT_TIMELY (5s, ack_ready);

	lumex::keypair key;
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	auto balance = lumex::dev::constants.genesis_amount;
	auto send_amount = node1->online_reps.delta () + 1;
	// Quick-confirm a block, legacy blocks should work without filtering
	{
		lumex::block_hash previous (node1->latest (lumex::dev::genesis_key.pub));
		balance -= send_amount;
		lumex::block_builder builder;
		auto send = builder
					.send ()
					.previous (previous)
					.destination (key.pub)
					.balance (balance)
					.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					.work (*system.work.generate (previous))
					.build ();
		node1->process_active (send);
	}

	ASSERT_TIMELY (5s, unsubscribed);

	// Quick confirm a state block
	{
		lumex::state_block_builder builder;
		lumex::block_hash previous (node1->latest (lumex::dev::genesis_key.pub));
		balance -= send_amount;
		auto send = builder
					.account (lumex::dev::genesis_key.pub)
					.previous (previous)
					.representative (lumex::dev::genesis_key.pub)
					.balance (balance)
					.link (key.pub)
					.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					.work (*system.work.generate (previous))
					.build ();

		node1->process_active (send);
	}

	ASSERT_TIMELY_EQ (5s, future.wait_for (0s), std::future_status::ready);
}

// Tests getting notification of a started election
TEST (websocket, started_election)
{
	lumex::test::system system;
	lumex::node_config config = system.default_config ();
	config.websocket_config->enabled = true;
	config.websocket_config->port = system.get_available_port ();
	auto node1 = system.add_node (config);

	std::atomic<bool> ack_ready{ false };
	auto task = ([&ack_ready, config, &node1] () {
		fake_websocket_client client (node1->websocket.server->listening_port ());
		client.send_message (R"json({"action": "subscribe", "topic": "started_election", "ack": "true"})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::started_election));
		return client.get_response ();
	});
	auto future = std::async (std::launch::async, task);

	ASSERT_TIMELY (5s, ack_ready);

	// Create election, causing a websocket message to be emitted
	lumex::keypair key1;
	lumex::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (0)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();
	lumex::messages::publish publish1{ lumex::dev::network_params.network, send1 };
	auto channel1 = std::make_shared<lumex::transport::fake::channel> (*node1);
	node1->inbound (publish1, channel1);
	ASSERT_TIMELY (1s, node1->active.election (send1->qualified_root ()));
	ASSERT_TIMELY_EQ (5s, future.wait_for (0s), std::future_status::ready);

	auto response = future.get ();
	ASSERT_TRUE (response);
	boost::property_tree::ptree event;
	std::stringstream stream;
	stream << response.value ();
	boost::property_tree::read_json (stream, event);
	ASSERT_EQ (event.get<std::string> ("topic"), "started_election");
}

// Tests getting notification of an erased election
TEST (websocket, stopped_election)
{
	lumex::test::system system;
	lumex::node_config config = system.default_config ();
	config.websocket_config->enabled = true;
	config.websocket_config->port = system.get_available_port ();
	auto node1 (system.add_node (config));

	std::atomic<bool> ack_ready{ false };
	auto task = ([&ack_ready, config, &node1] () {
		fake_websocket_client client (node1->websocket.server->listening_port ());
		client.send_message (R"json({"action": "subscribe", "topic": "stopped_election", "ack": "true"})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::stopped_election));
		return client.get_response ();
	});
	auto future = std::async (std::launch::async, task);

	ASSERT_TIMELY (5s, ack_ready);

	// Create election, then erase it, causing a websocket message to be emitted
	lumex::keypair key1;
	lumex::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (lumex::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (0)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();
	lumex::messages::publish publish1{ lumex::dev::network_params.network, send1 };
	auto channel1 = std::make_shared<lumex::transport::fake::channel> (*node1);
	node1->inbound (publish1, channel1);
	ASSERT_TIMELY (5s, node1->active.election (send1->qualified_root ()));
	node1->active.erase (*send1);

	ASSERT_TIMELY_EQ (5s, future.wait_for (0s), std::future_status::ready);

	auto response = future.get ();
	ASSERT_TRUE (response);
	boost::property_tree::ptree event;
	std::stringstream stream;
	stream << response.value ();
	boost::property_tree::read_json (stream, event);
	ASSERT_EQ (event.get<std::string> ("topic"), "stopped_election");
}

// Tests the filtering options of block confirmations
TEST (websocket, confirmation_options)
{
	lumex::test::system system;
	lumex::node_config config = system.default_config ();
	config.websocket_config->enabled = true;
	config.websocket_config->port = system.get_available_port ();
	auto node1 (system.add_node (config));

	std::atomic<bool> ack_ready{ false };
	auto task1 = ([&ack_ready, config, &node1] () {
		fake_websocket_client client (node1->websocket.server->listening_port ());
		client.send_message (R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {"confirmation_type": "active_quorum", "accounts": ["xrb_invalid"]}})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::confirmation));
		auto response = client.get_response (1s);
		EXPECT_FALSE (response);
	});
	auto future1 = std::async (std::launch::async, task1);

	ASSERT_TIMELY (5s, ack_ready);

	// Confirm a state block for an in-wallet account
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	lumex::keypair key;
	auto balance = lumex::dev::constants.genesis_amount;
	auto send_amount = node1->online_reps.delta () + 1;
	lumex::block_hash previous (node1->latest (lumex::dev::genesis_key.pub));
	{
		balance -= send_amount;
		lumex::state_block_builder builder;
		auto send = builder
					.account (lumex::dev::genesis_key.pub)
					.previous (previous)
					.representative (lumex::dev::genesis_key.pub)
					.balance (balance)
					.link (key.pub)
					.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					.work (*system.work.generate (previous))
					.build ();

		node1->process_active (send);
		previous = send->hash ();
	}

	ASSERT_TIMELY_EQ (5s, future1.wait_for (0s), std::future_status::ready);

	ack_ready = false;
	auto task2 = ([&ack_ready, config, &node1] () {
		fake_websocket_client client (node1->websocket.server->listening_port ());
		client.send_message (R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {"confirmation_type": "active_quorum", "all_local_accounts": "true", "include_election_info": "true"}})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::confirmation));
		return client.get_response ();
	});
	auto future2 = std::async (std::launch::async, task2);

	ASSERT_TIMELY (10s, ack_ready);

	// Quick-confirm another block
	{
		balance -= send_amount;
		lumex::state_block_builder builder;
		auto send = builder
					.account (lumex::dev::genesis_key.pub)
					.previous (previous)
					.representative (lumex::dev::genesis_key.pub)
					.balance (balance)
					.link (key.pub)
					.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					.work (*system.work.generate (previous))
					.build ();

		node1->process_active (send);
		previous = send->hash ();
	}

	ASSERT_TIMELY_EQ (5s, future2.wait_for (0s), std::future_status::ready);

	auto response2 = future2.get ();
	ASSERT_TRUE (response2);
	boost::property_tree::ptree event;
	std::stringstream stream;
	stream << response2.value ();
	boost::property_tree::read_json (stream, event);
	ASSERT_EQ (event.get<std::string> ("topic"), "confirmation");
	try
	{
		boost::property_tree::ptree election_info = event.get_child ("message.election_info");
		auto tally (election_info.get<std::string> ("tally"));
		auto final_tally (election_info.get<std::string> ("final"));
		auto time (election_info.get<std::string> ("time"));
		// Duration and request count may be zero on devnet, so we only check that they're present
		ASSERT_EQ (1, election_info.count ("duration"));
		ASSERT_EQ (1, election_info.count ("request_count"));
		ASSERT_EQ (1, election_info.count ("voters"));
		ASSERT_GE (1U, election_info.get<unsigned> ("blocks"));
		// Make sure tally and time are non-zero.
		ASSERT_NE ("0", tally);
		ASSERT_NE ("0", time);
		auto votes_l (election_info.get_child_optional ("votes"));
		ASSERT_FALSE (votes_l.has_value ());
	}
	catch (std::runtime_error const & ex)
	{
		FAIL () << ex.what ();
	}

	ack_ready = false;
	auto task3 = ([&ack_ready, config, &node1] () {
		fake_websocket_client client (node1->websocket.server->listening_port ());
		client.send_message (R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {"confirmation_type": "active_quorum", "all_local_accounts": "true"}})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::confirmation));
		auto response = client.get_response (1s);
		EXPECT_FALSE (response);
	});
	auto future3 = std::async (std::launch::async, task3);

	ASSERT_TIMELY (5s, ack_ready);

	// Confirm a legacy block
	// When filtering options are enabled, legacy blocks are always filtered
	{
		balance -= send_amount;
		lumex::block_builder builder;
		auto send = builder
					.send ()
					.previous (previous)
					.destination (key.pub)
					.balance (balance)
					.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					.work (*system.work.generate (previous))
					.build ();
		node1->process_active (send);
		previous = send->hash ();
	}

	ASSERT_TIMELY_EQ (5s, future3.wait_for (0s), std::future_status::ready);
}

TEST (websocket, confirmation_options_votes)
{
	lumex::test::system system;
	lumex::node_config config = system.default_config ();
	config.websocket_config->enabled = true;
	config.websocket_config->port = system.get_available_port ();
	auto node1 (system.add_node (config));

	std::atomic<bool> ack_ready{ false };
	auto task1 = ([&ack_ready, config, &node1] () {
		fake_websocket_client client (node1->websocket.server->listening_port ());
		client.send_message (R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {"confirmation_type": "active_quorum", "include_election_info_with_votes": "true", "include_block": "false"}})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::confirmation));
		return client.get_response ();
	});
	auto future1 = std::async (std::launch::async, task1);

	ASSERT_TIMELY (10s, ack_ready);

	// Confirm a state block for an in-wallet account
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	lumex::keypair key;
	auto balance = lumex::dev::constants.genesis_amount;
	auto send_amount = node1->config.online_weight_minimum.number () + 1;
	lumex::block_hash previous (node1->latest (lumex::dev::genesis_key.pub));
	{
		lumex::state_block_builder builder;
		balance -= send_amount;
		auto send = builder
					.account (lumex::dev::genesis_key.pub)
					.previous (previous)
					.representative (lumex::dev::genesis_key.pub)
					.balance (balance)
					.link (key.pub)
					.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					.work (*system.work.generate (previous))
					.build ();

		node1->process_active (send);
		previous = send->hash ();
	}

	ASSERT_TIMELY_EQ (5s, future1.wait_for (0s), std::future_status::ready);

	auto response1 = future1.get ();
	ASSERT_TRUE (response1);
	boost::property_tree::ptree event;
	std::stringstream stream;
	stream << response1.value ();
	boost::property_tree::read_json (stream, event);
	ASSERT_EQ (event.get<std::string> ("topic"), "confirmation");
	try
	{
		boost::property_tree::ptree election_info = event.get_child ("message.election_info");
		auto tally (election_info.get<std::string> ("tally"));
		auto time (election_info.get<std::string> ("time"));
		// Duration and request count may be zero on devnet, so we only check that they're present
		ASSERT_EQ (1, election_info.count ("duration"));
		ASSERT_EQ (1, election_info.count ("request_count"));
		ASSERT_EQ (1, election_info.count ("voters"));
		ASSERT_GE (1U, election_info.get<unsigned> ("blocks"));
		// Make sure tally and time are non-zero.
		ASSERT_NE ("0", tally);
		ASSERT_NE ("0", time);
		auto votes_l (election_info.get_child_optional ("votes"));
		ASSERT_TRUE (votes_l.has_value ());
		ASSERT_EQ (1, votes_l.value ().size ());
		for (auto & vote : votes_l.value ())
		{
			std::string representative (vote.second.get<std::string> ("representative"));
			ASSERT_EQ (lumex::dev::genesis_key.pub.to_account (), representative);
			std::string timestamp (vote.second.get<std::string> ("timestamp"));
			ASSERT_NE ("0", timestamp);
			std::string hash (vote.second.get<std::string> ("hash"));
			ASSERT_EQ (node1->latest (lumex::dev::genesis_key.pub).to_string (), hash);
			std::string weight (vote.second.get<std::string> ("weight"));
			ASSERT_EQ (node1->balance (lumex::dev::genesis_key.pub).convert_to<std::string> (), weight);
		}
	}
	catch (std::runtime_error const & ex)
	{
		FAIL () << ex.what ();
	}
}

TEST (websocket, confirmation_options_linked_account)
{
	lumex::test::system system;
	lumex::node_config config = system.default_config ();
	config.websocket_config->enabled = true;
	config.websocket_config->port = system.get_available_port ();
	auto node1 (system.add_node (config));

	std::atomic<bool> ack_ready{ false };
	auto task1 = ([&ack_ready, config, &node1] () {
		fake_websocket_client client (node1->websocket.server->listening_port ());
		client.send_message (R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {"confirmation_type": "active_quorum", "include_block": "true", "include_linked_account": "true"}})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::confirmation));
		return client.get_response ();
	});
	auto future1 = std::async (std::launch::async, task1);

	ASSERT_TIMELY (10s, ack_ready);

	// Confirm a state block for an in-wallet account
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	lumex::keypair key;
	auto balance = lumex::dev::constants.genesis_amount;
	auto send_amount = node1->config.online_weight_minimum.number () + 1;
	lumex::block_hash previous (node1->latest (lumex::dev::genesis_key.pub));
	{
		lumex::state_block_builder builder;
		balance -= send_amount;
		auto send = builder
					.account (lumex::dev::genesis_key.pub)
					.previous (previous)
					.representative (lumex::dev::genesis_key.pub)
					.balance (balance)
					.link (key.pub)
					.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					.work (*system.work.generate (previous))
					.build ();

		node1->process_active (send);
		previous = send->hash ();
	}

	ASSERT_TIMELY_EQ (5s, future1.wait_for (0s), std::future_status::ready);

	auto response1 = future1.get ();
	ASSERT_TRUE (response1);
	boost::property_tree::ptree event;
	std::stringstream stream;
	stream << response1.value ();
	boost::property_tree::read_json (stream, event);
	ASSERT_EQ (event.get<std::string> ("topic"), "confirmation");
	try
	{
		boost::property_tree::ptree block_content = event.get_child ("message.block");
		// Check if linked_account is present
		ASSERT_EQ (1, block_content.count ("linked_account"));
		// Make sure linked_account is non-zero.
		ASSERT_NE ("0", block_content.get<std::string> ("linked_account"));
	}
	catch (std::runtime_error const & ex)
	{
		FAIL () << ex.what ();
	}

	ack_ready = false;
	auto task2 = ([&ack_ready, config, &node1] () {
		fake_websocket_client client (node1->websocket.server->listening_port ());
		client.send_message (R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {"confirmation_type": "active_quorum", "include_block": "true", "include_linked_account": "true"}})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::confirmation));
		return client.get_response ();
	});
	auto future2 = std::async (std::launch::async, task2);

	ASSERT_TIMELY (10s, ack_ready);

	// Quick-confirm a receive block
	{
		lumex::state_block_builder builder;
		balance = send_amount;
		auto open = builder
					.account (key.pub)
					.previous (0)
					.representative (lumex::dev::genesis_key.pub)
					.balance (balance)
					.link (previous)
					.sign (key.prv, key.pub)
					.work (*system.work.generate (key.pub))
					.build ();

		node1->process_active (open);
		previous = open->hash ();
	}

	ASSERT_TIMELY_EQ (5s, future2.wait_for (0s), std::future_status::ready);

	auto response2 = future2.get ();
	ASSERT_TRUE (response2);
	boost::property_tree::ptree event2;
	std::stringstream stream2;
	stream2 << response2.value ();
	boost::property_tree::read_json (stream2, event2);
	ASSERT_EQ (event2.get<std::string> ("topic"), "confirmation");
	try
	{
		boost::property_tree::ptree block_content = event2.get_child ("message.block");
		// Check if linked_account is present
		ASSERT_EQ (1, block_content.count ("linked_account"));
		// Make sure linked_account is non-zero.
		ASSERT_NE ("0", block_content.get<std::string> ("linked_account"));
	}
	catch (std::runtime_error const & ex)
	{
		FAIL () << ex.what ();
	}

	ack_ready = false;
	auto task3 = ([&ack_ready, config, &node1] () {
		fake_websocket_client client (node1->websocket.server->listening_port ());
		client.send_message (R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {"confirmation_type": "active_quorum", "include_block": "true", "include_linked_account": "true"}})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::confirmation));
		return client.get_response ();
	});
	auto future3 = std::async (std::launch::async, task3);

	ASSERT_TIMELY (10s, ack_ready);

	// Quick-confirm a change block
	{
		lumex::state_block_builder builder;
		auto change = builder
					  .account (key.pub)
					  .previous (previous)
					  .representative (key.pub)
					  .balance (balance)
					  .link (0)
					  .sign (key.prv, key.pub)
					  .work (*system.work.generate (previous))
					  .build ();

		node1->process_active (change);
	}

	ASSERT_TIMELY_EQ (5s, future3.wait_for (0s), std::future_status::ready);

	auto response3 = future3.get ();
	ASSERT_TRUE (response3);
	boost::property_tree::ptree event3;
	std::stringstream stream3;
	stream3 << response3.value ();
	boost::property_tree::read_json (stream3, event3);
	ASSERT_EQ (event3.get<std::string> ("topic"), "confirmation");
	try
	{
		boost::property_tree::ptree block_content = event3.get_child ("message.block");
		// Check if linked_account is present
		ASSERT_EQ (1, block_content.count ("linked_account"));
		// Make sure linked_account is zero.
		ASSERT_EQ ("0", block_content.get<std::string> ("linked_account"));
	}
	catch (std::runtime_error const & ex)
	{
		FAIL () << ex.what ();
	}
}

TEST (websocket, confirmation_options_sideband)
{
	lumex::test::system system;
	lumex::node_config config = system.default_config ();
	config.websocket_config->enabled = true;
	config.websocket_config->port = system.get_available_port ();
	auto node1 (system.add_node (config));

	std::atomic<bool> ack_ready{ false };
	auto task1 = ([&ack_ready, config, &node1] () {
		fake_websocket_client client (node1->websocket.server->listening_port ());
		client.send_message (R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {"confirmation_type": "active_quorum", "include_block": "false", "include_sideband_info": "true"}})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::confirmation));
		return client.get_response ();
	});
	auto future1 = std::async (std::launch::async, task1);

	ASSERT_TIMELY (10s, ack_ready);

	// Confirm a state block for an in-wallet account
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	lumex::keypair key;
	auto balance = lumex::dev::constants.genesis_amount;
	auto send_amount = node1->config.online_weight_minimum.number () + 1;
	lumex::block_hash previous (node1->latest (lumex::dev::genesis_key.pub));
	{
		lumex::state_block_builder builder;
		balance -= send_amount;
		auto send = builder
					.account (lumex::dev::genesis_key.pub)
					.previous (previous)
					.representative (lumex::dev::genesis_key.pub)
					.balance (balance)
					.link (key.pub)
					.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					.work (*system.work.generate (previous))
					.build ();

		node1->process_active (send);
		previous = send->hash ();
	}

	ASSERT_TIMELY_EQ (5s, future1.wait_for (0s), std::future_status::ready);

	auto response1 = future1.get ();
	ASSERT_TRUE (response1);
	boost::property_tree::ptree event;
	std::stringstream stream;
	stream << response1.value ();
	boost::property_tree::read_json (stream, event);
	ASSERT_EQ (event.get<std::string> ("topic"), "confirmation");
	try
	{
		boost::property_tree::ptree sideband_info = event.get_child ("message.sideband");
		// Check if height and local_timestamp are present
		ASSERT_EQ (1, sideband_info.count ("height"));
		ASSERT_EQ (1, sideband_info.count ("local_timestamp"));
		// Make sure height and local_timestamp are non-zero.
		ASSERT_NE ("0", sideband_info.get<std::string> ("height"));
		ASSERT_NE ("0", sideband_info.get<std::string> ("local_timestamp"));
	}
	catch (std::runtime_error const & ex)
	{
		FAIL () << ex.what ();
	}
}

// Tests updating options of block confirmations
TEST (websocket, confirmation_options_update)
{
	lumex::test::system system;
	lumex::node_config config = system.default_config ();
	config.websocket_config->enabled = true;
	config.websocket_config->port = system.get_available_port ();
	auto node1 (system.add_node (config));

	std::atomic<bool> added{ false };
	std::atomic<bool> deleted{ false };
	auto task = ([&added, &deleted, config, &node1] () {
		fake_websocket_client client (node1->websocket.server->listening_port ());
		// Subscribe initially with empty options, everything will be filtered
		client.send_message (R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {}})json");
		client.await_ack ();
		EXPECT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::confirmation));
		// Now update filter with an account and wait for a response
		std::string add_message = boost::str (boost::format (R"json({"action": "update", "topic": "confirmation", "ack": "true", "options": {"accounts_add": ["%1%"]}})json") % lumex::dev::genesis_key.pub.to_account ());
		client.send_message (add_message);
		client.await_ack ();
		EXPECT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::confirmation));
		added = true;
		EXPECT_TRUE (client.get_response ());
		// Update the filter again, removing the account
		std::string delete_message = boost::str (boost::format (R"json({"action": "update", "topic": "confirmation", "ack": "true", "options": {"accounts_del": ["%1%"]}})json") % lumex::dev::genesis_key.pub.to_account ());
		client.send_message (delete_message);
		client.await_ack ();
		EXPECT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::confirmation));
		deleted = true;
		EXPECT_FALSE (client.get_response (1s));
	});
	auto future = std::async (std::launch::async, task);

	// Wait for update acknowledgement
	ASSERT_TIMELY (5s, added);

	// Confirm a block
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	lumex::keypair key;
	lumex::state_block_builder builder;
	auto previous (node1->latest (lumex::dev::genesis_key.pub));
	auto send = builder
				.account (lumex::dev::genesis_key.pub)
				.previous (previous)
				.representative (lumex::dev::genesis_key.pub)
				.balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				.link (key.pub)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*system.work.generate (previous))
				.build ();

	node1->process_active (send);

	// Wait for delete acknowledgement
	ASSERT_TIMELY (5s, deleted);

	// Confirm another block
	previous = send->hash ();
	auto send2 = builder
				 .make_block ()
				 .account (lumex::dev::genesis_key.pub)
				 .previous (previous)
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - 2 * lumex::Klumex_ratio)
				 .link (key.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (previous))
				 .build ();

	node1->process_active (send2);

	ASSERT_TIMELY_EQ (5s, future.wait_for (0s), std::future_status::ready);
}

// Subscribes to votes, sends a block and awaits websocket notification of a vote arrival
TEST (websocket, vote)
{
	lumex::test::system system;
	lumex::node_config config = system.default_config ();
	config.websocket_config->enabled = true;
	config.websocket_config->port = system.get_available_port ();
	auto node1 (system.add_node (config));

	std::atomic<bool> ack_ready{ false };
	auto task = ([&ack_ready, config, &node1] () {
		fake_websocket_client client (node1->websocket.server->listening_port ());
		client.send_message (R"json({"action": "subscribe", "topic": "vote", "ack": true})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::vote));
		return client.get_response ();
	});
	auto future = std::async (std::launch::async, task);

	ASSERT_TIMELY (5s, ack_ready);

	// Quick-confirm a block
	lumex::keypair key;
	lumex::state_block_builder builder;
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	lumex::block_hash previous (node1->latest (lumex::dev::genesis_key.pub));
	auto send = builder
				.account (lumex::dev::genesis_key.pub)
				.previous (previous)
				.representative (lumex::dev::genesis_key.pub)
				.balance (lumex::dev::constants.genesis_amount - (node1->online_reps.delta () + 1))
				.link (key.pub)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*system.work.generate (previous))
				.build ();

	node1->process_active (send);

	ASSERT_TIMELY_EQ (5s, future.wait_for (0s), std::future_status::ready);

	auto response = future.get ();
	ASSERT_TRUE (response);
	boost::property_tree::ptree event;
	std::stringstream stream;
	stream << response.value ();
	boost::property_tree::read_json (stream, event);
	ASSERT_EQ (event.get<std::string> ("topic"), "vote");
	auto message_contents = event.get_child ("message");
	ASSERT_EQ (message_contents.count ("account"), 1);
	ASSERT_EQ (message_contents.count ("signature"), 1);
	ASSERT_EQ (message_contents.count ("sequence"), 1);
	ASSERT_EQ (message_contents.count ("timestamp"), 1);
	ASSERT_EQ (message_contents.count ("duration"), 1);
	ASSERT_EQ (message_contents.count ("blocks"), 1);
	ASSERT_EQ (message_contents.count ("type"), 1);

	lumex::signature signature;
	std::string signature_text (message_contents.get<std::string> ("signature"));
	ASSERT_EQ (signature_text.size (), 128);
	ASSERT_FALSE (signature.decode_hex (signature_text));
}

// Tests vote subscription options - vote type
TEST (websocket, vote_options_type)
{
	lumex::test::system system;
	lumex::node_config config = system.default_config ();
	config.websocket_config->enabled = true;
	config.websocket_config->port = system.get_available_port ();
	auto node1 (system.add_node (config));

	std::atomic<bool> ack_ready{ false };
	auto task = ([&ack_ready, config, &node1] () {
		fake_websocket_client client (node1->websocket.server->listening_port ());
		client.send_message (R"json({"action": "subscribe", "topic": "vote", "ack": true, "options": {"include_replays": "true", "include_indeterminate": "false"}})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::vote));
		return client.get_response ();
	});
	auto future = std::async (std::launch::async, task);

	ASSERT_TIMELY (5s, ack_ready);

	// Custom made votes for simplicity
	auto vote = lumex::test::make_vote (lumex::dev::genesis_key, { lumex::dev::genesis }, 0, 0);
	lumex::websocket::message_builder builder{ node1->ledger };
	auto msg (builder.vote_received (vote, lumex::vote_code::replay));
	node1->websocket.server->broadcast (msg);

	ASSERT_TIMELY_EQ (5s, future.wait_for (0s), std::future_status::ready);

	auto response = future.get ();
	ASSERT_TRUE (response);
	boost::property_tree::ptree event;
	std::stringstream stream;
	stream << response.value ();
	boost::property_tree::read_json (stream, event);
	auto message_contents = event.get_child ("message");
	ASSERT_EQ (1, message_contents.count ("type"));
	ASSERT_EQ ("replay", message_contents.get<std::string> ("type"));
}

// Tests vote subscription options - list of representatives
TEST (websocket, vote_options_representatives)
{
	lumex::test::system system;
	lumex::node_config config = system.default_config ();
	config.websocket_config->enabled = true;
	config.websocket_config->port = system.get_available_port ();
	auto node1 (system.add_node (config));

	std::atomic<bool> ack_ready{ false };
	auto task1 = ([&ack_ready, config, &node1] () {
		fake_websocket_client client (node1->websocket.server->listening_port ());
		std::string message = boost::str (boost::format (R"json({"action": "subscribe", "topic": "vote", "ack": "true", "options": {"representatives": ["%1%"]}})json") % lumex::dev::genesis_key.pub.to_account ());
		client.send_message (message);
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::vote));
		auto response = client.get_response ();
		EXPECT_TRUE (response);
		boost::property_tree::ptree event;
		std::stringstream stream;
		stream << response.value ();
		boost::property_tree::read_json (stream, event);
		EXPECT_EQ (event.get<std::string> ("topic"), "vote");
	});
	auto future1 = std::async (std::launch::async, task1);

	ASSERT_TIMELY (5s, ack_ready);

	// Quick-confirm a block
	lumex::keypair key;
	auto balance = lumex::dev::constants.genesis_amount;
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	auto send_amount = node1->online_reps.delta () + 1;
	auto confirm_block = [&] () {
		lumex::block_hash previous (node1->latest (lumex::dev::genesis_key.pub));
		balance -= send_amount;
		lumex::state_block_builder builder;
		auto send = builder
					.account (lumex::dev::genesis_key.pub)
					.previous (previous)
					.representative (lumex::dev::genesis_key.pub)
					.balance (balance)
					.link (key.pub)
					.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					.work (*system.work.generate (previous))
					.build ();
		node1->process_active (send);
	};
	confirm_block ();

	ASSERT_TIMELY_EQ (5s, future1.wait_for (0s), std::future_status::ready);

	ack_ready = false;
	auto task2 = ([&ack_ready, config, &node1] () {
		fake_websocket_client client (node1->websocket.server->listening_port ());
		client.send_message (R"json({"action": "subscribe", "topic": "vote", "ack": "true", "options": {"representatives": ["xrb_invalid"]}})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::vote));
		auto response = client.get_response ();
		// A list of invalid representatives is the same as no filter
		EXPECT_TRUE (response);
	});
	auto future2 = std::async (std::launch::async, task2);

	// Wait for the subscription to be acknowledged
	ASSERT_TIMELY (5s, ack_ready);

	// Confirm another block
	confirm_block ();

	ASSERT_TIMELY_EQ (5s, future2.wait_for (0s), std::future_status::ready);
}

// Test client subscribing to notifications for work generation
TEST (websocket, work)
{
	lumex::test::system system;
	lumex::node_config config = system.default_config ();
	config.websocket_config->enabled = true;
	config.websocket_config->port = system.get_available_port ();
	auto node1 (system.add_node (config));

	ASSERT_EQ (0, node1->websocket.server->subscriber_count (lumex::websocket::topic::work));

	// Subscribe to work and wait for response asynchronously
	std::atomic<bool> ack_ready{ false };
	auto task = ([&ack_ready, config, &node1] () {
		fake_websocket_client client (node1->websocket.server->listening_port ());
		client.send_message (R"json({"action": "subscribe", "topic": "work", "ack": true})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::work));
		return client.get_response ();
	});
	auto future = std::async (std::launch::async, task);

	// Wait for acknowledge
	ASSERT_TIMELY (5s, ack_ready);
	ASSERT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::work));

	// Generate work
	lumex::block_hash hash{ 1 };
	auto work (node1->work_generate_blocking (hash));
	ASSERT_TRUE (work.has_value ());

	// Wait for the work notification
	ASSERT_TIMELY_EQ (5s, future.wait_for (0s), std::future_status::ready);

	// Check the work notification message
	auto response = future.get ();
	ASSERT_TRUE (response);
	std::stringstream stream;
	stream << response.value ();
	boost::property_tree::ptree event;
	boost::property_tree::read_json (stream, event);
	ASSERT_EQ (event.get<std::string> ("topic"), "work");

	auto & contents = event.get_child ("message");
	ASSERT_EQ (contents.get<std::string> ("success"), "true");
	ASSERT_LT (contents.get<unsigned> ("duration"), 10000U);

	ASSERT_EQ (1, contents.count ("request"));
	auto & request = contents.get_child ("request");
	ASSERT_EQ (request.get<std::string> ("version"), lumex::to_string (lumex::work_version::work_1));
	ASSERT_EQ (request.get<std::string> ("hash"), hash.to_string ());
	ASSERT_EQ (request.get<std::string> ("difficulty"), lumex::to_string_hex (node1->default_difficulty (lumex::work_version::work_1)));
	ASSERT_EQ (request.get<double> ("multiplier"), 1.0);

	ASSERT_EQ (1, contents.count ("result"));
	auto & result = contents.get_child ("result");
	uint64_t result_difficulty;
	lumex::from_string_hex (result.get<std::string> ("difficulty"), result_difficulty);
	ASSERT_GE (result_difficulty, node1->default_difficulty (lumex::work_version::work_1));
	ASSERT_NEAR (result.get<double> ("multiplier"), lumex::difficulty::to_multiplier (result_difficulty, node1->default_difficulty (lumex::work_version::work_1)), 1e-6);
	ASSERT_EQ (result.get<std::string> ("work"), lumex::to_string_hex (work.value ()));

	ASSERT_EQ (1, contents.count ("bad_peers"));
	auto & bad_peers = contents.get_child ("bad_peers");
	ASSERT_TRUE (bad_peers.empty ());

	ASSERT_EQ (contents.get<std::string> ("reason"), "");
}

// Tests sending keepalive
TEST (websocket, ws_keepalive)
{
	lumex::test::system system;
	lumex::node_config config = system.default_config ();
	config.websocket_config->enabled = true;
	config.websocket_config->port = system.get_available_port ();
	auto node1 (system.add_node (config));

	auto task = ([&node1] () {
		fake_websocket_client client (node1->websocket.server->listening_port ());
		client.send_message (R"json({"action": "ping"})json");
		client.await_ack ();
	});
	auto future = std::async (std::launch::async, task);

	ASSERT_TIMELY_EQ (5s, future.wait_for (0s), std::future_status::ready);
}

// Tests sending telemetry
TEST (websocket, telemetry)
{
	lumex::test::system system;
	lumex::node_config config = system.default_config ();
	config.websocket_config->enabled = true;
	config.websocket_config->port = system.get_available_port ();
	lumex::node_flags node_flags;
	auto node1 (system.add_node (config, node_flags));
	config.peering_port = system.get_available_port ();
	config.websocket_config->enabled = true;
	config.websocket_config->port = system.get_available_port ();
	auto node2 (system.add_node (config, node_flags));

	std::atomic<bool> done{ false };
	auto task = ([config = node1->config, &node1, &done] () {
		fake_websocket_client client (node1->websocket.server->listening_port ());
		client.send_message (R"json({"action": "subscribe", "topic": "telemetry", "ack": true})json");
		client.await_ack ();
		done = true;
		EXPECT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::telemetry));
		return client.get_response ();
	});

	auto future = std::async (std::launch::async, task);

	ASSERT_TIMELY (10s, done);

	auto channel = node1->network.find_node_id (node2->get_node_id ());
	ASSERT_NE (channel, nullptr);
	ASSERT_TIMELY (5s, node1->telemetry.get_telemetry (channel->get_remote_endpoint ()));

	ASSERT_TIMELY_EQ (10s, future.wait_for (0s), std::future_status::ready);

	// Check the telemetry notification message
	auto response = future.get ();

	std::stringstream stream;
	stream << response.value ();
	boost::property_tree::ptree event;
	boost::property_tree::read_json (stream, event);
	ASSERT_EQ (event.get<std::string> ("topic"), "telemetry");

	auto & contents = event.get_child ("message");
	lumex::jsonconfig telemetry_contents (contents);
	lumex::messages::telemetry_data telemetry_data;
	telemetry_data.deserialize_json (telemetry_contents, false);

	ASSERT_TRUE (lumex::test::compare_telemetry (telemetry_data, *node2));

	auto channel2 = node2->network.find_node_id (node1->get_node_id ());
	ASSERT_NE (channel2, nullptr);

	ASSERT_EQ (contents.get<std::string> ("address"), channel2->get_local_endpoint ().address ().to_string ());
	ASSERT_EQ (contents.get<uint16_t> ("port"), channel2->get_local_endpoint ().port ());

	// Other node should have no subscribers
	EXPECT_EQ (0, node2->websocket.server->subscriber_count (lumex::websocket::topic::telemetry));
}

TEST (websocket, new_unconfirmed_block)
{
	lumex::test::system system;
	lumex::node_config config = system.default_config ();
	config.websocket_config->enabled = true;
	config.websocket_config->port = system.get_available_port ();
	auto node1 (system.add_node (config));

	std::atomic<bool> ack_ready{ false };
	auto task = ([&ack_ready, config, node1] () {
		fake_websocket_client client (node1->websocket.server->listening_port ());
		client.send_message (R"json({"action": "subscribe", "topic": "new_unconfirmed_block", "ack": "true"})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket.server->subscriber_count (lumex::websocket::topic::new_unconfirmed_block));
		return client.get_response ();
	});
	auto future = std::async (std::launch::async, task);

	ASSERT_TIMELY (5s, ack_ready);

	lumex::state_block_builder builder;
	// Process a new block
	auto send1 = builder
				 .account (lumex::dev::genesis_key.pub)
				 .previous (lumex::dev::genesis->hash ())
				 .representative (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - 1)
				 .link (lumex::dev::genesis_key.pub)
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*system.work.generate (lumex::dev::genesis->hash ()))
				 .build ();

	ASSERT_EQ (lumex::block_status::progress, node1->process_local (send1).value ());

	ASSERT_TIMELY_EQ (5s, future.wait_for (0s), std::future_status::ready);

	// Check the response
	std::optional<std::string> response = future.get ();
	ASSERT_TRUE (response);
	std::stringstream stream;
	stream << response.value ();
	boost::property_tree::ptree event;
	boost::property_tree::read_json (stream, event);
	ASSERT_EQ (event.get<std::string> ("topic"), "new_unconfirmed_block");
	ASSERT_EQ (event.get<std::string> ("hash"), send1->hash ().to_string ());

	auto message_contents = event.get_child ("message");
	ASSERT_EQ ("state", message_contents.get<std::string> ("type"));
	ASSERT_EQ ("send", message_contents.get<std::string> ("subtype"));
}

// Test verifying that multiple subscribers with different options receive messages with their correct
// individual settings applied (specifically targeting the bug that was fixed)
TEST (websocket, confirmation_options_independent)
{
	lumex::test::system system;
	lumex::node_config config = system.default_config ();
	config.websocket_config->enabled = true;
	config.websocket_config->port = system.get_available_port ();
	auto node1 (system.add_node (config));

	// First prepare a block we'll confirm later
	lumex::keypair key;
	lumex::state_block_builder builder;
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	auto prev_balance = lumex::dev::constants.genesis_amount;
	auto send_amount = node1->online_reps.delta () + 1;
	auto new_balance = prev_balance - send_amount;
	lumex::block_hash previous (node1->latest (lumex::dev::genesis_key.pub));

	auto send = builder
				.account (lumex::dev::genesis_key.pub)
				.previous (previous)
				.representative (lumex::dev::genesis_key.pub)
				.balance (new_balance)
				.link (key.pub)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*system.work.generate (previous))
				.build ();

	// Set up two concurrent tasks to subscribe with different options and wait for responses
	std::atomic<bool> client1_done{ false };
	std::atomic<bool> client2_done{ false };
	std::optional<std::string> client1_response;
	std::optional<std::string> client2_response;

	// Client 1: Subscribe with include_block = true but no sideband
	auto client1_task = ([&client1_done, &client1_response, &node1] () {
		fake_websocket_client client (node1->websocket.server->listening_port ());
		client.send_message (R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {"include_block": "true", "include_sideband_info": "false"}})json");
		client.await_ack ();
		auto response = client.get_response ();
		client1_response = response;
		client1_done = true;
	});

	// Client 2: Subscribe with include_block = true AND include_sideband_info = true
	auto client2_task = ([&client2_done, &client2_response, &node1] () {
		fake_websocket_client client (node1->websocket.server->listening_port ());
		client.send_message (R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {"include_block": "true", "include_sideband_info": "true"}})json");
		client.await_ack ();
		auto response = client.get_response ();
		client2_response = response;
		client2_done = true;
	});

	// Start both client tasks concurrently
	auto future1 = std::async (std::launch::async, client1_task);
	auto future2 = std::async (std::launch::async, client2_task);

	// Wait for both clients to be set up (both awaiting notifications)
	ASSERT_TIMELY (5s, node1->websocket.server->subscriber_count (lumex::websocket::topic::confirmation) == 2);

	// Now process the block to trigger notifications to both clients
	node1->process_active (send);

	// Wait for both clients to receive their responses
	ASSERT_TIMELY (5s, client1_done && client2_done);

	// Verify both clients got responses
	ASSERT_TRUE (client1_response.has_value ());
	ASSERT_TRUE (client2_response.has_value ());

	// Parse and check client1 response (should have block but no sideband)
	boost::property_tree::ptree event1;
	std::stringstream stream1;
	stream1 << client1_response.value ();
	boost::property_tree::read_json (stream1, event1);
	ASSERT_EQ (event1.get<std::string> ("topic"), "confirmation");

	auto & message1 = event1.get_child ("message");
	ASSERT_EQ (1, message1.count ("block"));
	ASSERT_EQ (0, message1.count ("sideband"));

	// Parse and check client2 response (should have both block AND sideband)
	boost::property_tree::ptree event2;
	std::stringstream stream2;
	stream2 << client2_response.value ();
	boost::property_tree::read_json (stream2, event2);
	ASSERT_EQ (event2.get<std::string> ("topic"), "confirmation");

	auto & message2 = event2.get_child ("message");
	ASSERT_EQ (1, message2.count ("block"));

	// With the old caching code, this would fail because client2 would receive the same
	// message as client1 (with no sideband info) despite requesting it
	ASSERT_EQ (1, message2.count ("sideband"));

	// Verify sideband contains expected fields
	auto & sideband = message2.get_child ("sideband");
	ASSERT_EQ (1, sideband.count ("height"));
	ASSERT_EQ (1, sideband.count ("local_timestamp"));
}