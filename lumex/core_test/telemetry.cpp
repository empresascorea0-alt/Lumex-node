#include <lumex/lib/stream.hpp>
#include <lumex/messages/telemetry.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/telemetry.hpp>
#include <lumex/node/transport/fake.hpp>
#include <lumex/test_common/network.hpp>
#include <lumex/test_common/system.hpp>
#include <lumex/test_common/telemetry.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/endian/conversion.hpp>

#include <numeric>

using namespace std::chrono_literals;

TEST (telemetry, signatures)
{
	lumex::keypair node_id;
	lumex::messages::telemetry_data data;
	data.node_id = node_id.pub;
	data.major_version = 20;
	data.minor_version = 1;
	data.patch_version = 5;
	data.pre_release_version = 2;
	data.maker = lumex::messages::telemetry_maker::nf_pruned_node;
	data.database_backend = lumex::messages::telemetry_database_backend::lmdb;
	data.confirmation_latency_ms_p50 = 250;
	data.confirmation_latency_ms_p90 = 500;
	data.confirmation_latency_ms_p99 = 750;
	data.bootstrap_status = lumex::messages::telemetry_bootstrap_status::synced;
	data.timestamp = std::chrono::system_clock::time_point (100ms);
	data.sign (node_id);
	ASSERT_FALSE (data.validate_signature ());
	auto signature = data.signature;
	// Check that the signature is different if changing a piece of data
	data.maker = lumex::messages::telemetry_maker::nf_node;
	data.sign (node_id);
	ASSERT_NE (data.signature, signature);
}

TEST (telemetry, unknown_data)
{
	lumex::keypair node_id;
	lumex::messages::telemetry_data data;
	data.node_id = node_id.pub;
	data.major_version = 20;
	data.minor_version = 1;
	data.patch_version = 5;
	data.pre_release_version = 2;
	data.maker = lumex::messages::telemetry_maker::nf_pruned_node;
	data.timestamp = std::chrono::system_clock::time_point (100ms);
	data.unknown_data.push_back (1);
	data.sign (node_id);
	ASSERT_FALSE (data.validate_signature ());
}

// Old node (size_v1 payload) -> New node: signature must validate, new fields default to 0
TEST (telemetry, backward_compat_v1_to_new)
{
	lumex::keypair node_id;
	lumex::messages::telemetry_data data;
	data.node_id = node_id.pub;
	data.major_version = 20;
	data.minor_version = 1;
	data.patch_version = 5;
	data.pre_release_version = 2;
	data.maker = lumex::messages::telemetry_maker::nf_pruned_node;
	data.timestamp = std::chrono::system_clock::time_point (100ms);
	data.active_difficulty = 0xffffffc000000000;

	// Simulate old node: serialize with size_v1 (no v2 fields)
	std::vector<uint8_t> old_payload;
	{
		lumex::vectorstream stream (old_payload);
		lumex::write (stream, data.signature);
		lumex::write (stream, data.node_id);
		lumex::write (stream, boost::endian::native_to_big (data.block_count));
		lumex::write (stream, boost::endian::native_to_big (data.cemented_count));
		lumex::write (stream, boost::endian::native_to_big (data.unchecked_count));
		lumex::write (stream, boost::endian::native_to_big (data.account_count));
		lumex::write (stream, boost::endian::native_to_big (data.bandwidth_cap));
		lumex::write (stream, boost::endian::native_to_big (data.peer_count));
		lumex::write (stream, data.protocol_version);
		lumex::write (stream, boost::endian::native_to_big (data.uptime));
		lumex::write (stream, data.genesis_block.bytes);
		lumex::write (stream, data.major_version);
		lumex::write (stream, data.minor_version);
		lumex::write (stream, data.patch_version);
		lumex::write (stream, data.pre_release_version);
		lumex::write (stream, static_cast<uint8_t> (data.maker));
		lumex::write (stream, boost::endian::native_to_big (std::chrono::duration_cast<std::chrono::milliseconds> (data.timestamp.time_since_epoch ()).count ()));
		lumex::write (stream, boost::endian::native_to_big (data.active_difficulty));
	}

	// Sign using only old-format fields (without signature prefix)
	std::vector<uint8_t> sign_bytes (old_payload.begin () + sizeof (lumex::signature), old_payload.end ());
	auto sig = lumex::sign_message (node_id.prv, node_id.pub, sign_bytes.data (), sign_bytes.size ());
	std::copy (sig.bytes.begin (), sig.bytes.end (), old_payload.begin ());

	lumex::messages::telemetry_data received;
	lumex::bufferstream stream (old_payload.data (), old_payload.size ());
	received.deserialize (stream, lumex::messages::telemetry_data::size_v1);

	ASSERT_EQ (received.version, lumex::messages::telemetry_data_version::v1);
	ASSERT_EQ (received.database_backend, lumex::messages::telemetry_database_backend::unknown);
	ASSERT_EQ (received.confirmation_latency_ms_p50, 0);
	ASSERT_EQ (received.confirmation_latency_ms_p90, 0);
	ASSERT_EQ (received.confirmation_latency_ms_p99, 0);
	ASSERT_EQ (received.bootstrap_status, lumex::messages::telemetry_bootstrap_status::unknown);
	ASSERT_FALSE (received.validate_signature ());
	ASSERT_EQ (received.node_id, node_id.pub);
}

// New node (v2 payload) -> Old v1 node: extra v2 bytes go to unknown_data, signature validates
TEST (telemetry, forward_compat_new_to_old)
{
	lumex::keypair node_id;
	lumex::messages::telemetry_data data;
	data.node_id = node_id.pub;
	data.major_version = 20;
	data.minor_version = 1;
	data.patch_version = 5;
	data.pre_release_version = 2;
	data.maker = lumex::messages::telemetry_maker::nf_pruned_node;
	data.database_backend = lumex::messages::telemetry_database_backend::lmdb;
	data.confirmation_latency_ms_p50 = 100;
	data.confirmation_latency_ms_p90 = 250;
	data.confirmation_latency_ms_p99 = 500;
	data.bootstrap_status = lumex::messages::telemetry_bootstrap_status::synced;
	data.timestamp = std::chrono::system_clock::time_point (100ms);
	data.active_difficulty = 0xffffffc000000000;
	data.sign (node_id);
	ASSERT_FALSE (data.validate_signature ());

	// Serialize the new-format data (size_v2)
	std::vector<uint8_t> payload;
	{
		lumex::vectorstream stream (payload);
		data.serialize (stream);
	}
	ASSERT_EQ (payload.size (), lumex::messages::telemetry_data::size);

	// Simulate old v1 node deserializing: it only knows size_v1 as its latest_size
	lumex::messages::telemetry_data old_received;
	uint16_t const old_latest_size = lumex::messages::telemetry_data::size_v1;
	{
		lumex::bufferstream stream (payload.data (), payload.size ());
		lumex::read (stream, old_received.signature);
		lumex::read (stream, old_received.node_id);
		lumex::read (stream, old_received.block_count);
		boost::endian::big_to_native_inplace (old_received.block_count);
		lumex::read (stream, old_received.cemented_count);
		boost::endian::big_to_native_inplace (old_received.cemented_count);
		lumex::read (stream, old_received.unchecked_count);
		boost::endian::big_to_native_inplace (old_received.unchecked_count);
		lumex::read (stream, old_received.account_count);
		boost::endian::big_to_native_inplace (old_received.account_count);
		lumex::read (stream, old_received.bandwidth_cap);
		boost::endian::big_to_native_inplace (old_received.bandwidth_cap);
		lumex::read (stream, old_received.peer_count);
		boost::endian::big_to_native_inplace (old_received.peer_count);
		lumex::read (stream, old_received.protocol_version);
		lumex::read (stream, old_received.uptime);
		boost::endian::big_to_native_inplace (old_received.uptime);
		lumex::read (stream, old_received.genesis_block.bytes);
		lumex::read (stream, old_received.major_version);
		lumex::read (stream, old_received.minor_version);
		lumex::read (stream, old_received.patch_version);
		lumex::read (stream, old_received.pre_release_version);
		uint8_t maker_l;
		lumex::read (stream, maker_l);
		old_received.maker = static_cast<lumex::messages::telemetry_maker> (maker_l);
		uint64_t timestamp_l;
		lumex::read (stream, timestamp_l);
		boost::endian::big_to_native_inplace (timestamp_l);
		old_received.timestamp = std::chrono::system_clock::time_point (std::chrono::milliseconds (timestamp_l));
		lumex::read (stream, old_received.active_difficulty);
		boost::endian::big_to_native_inplace (old_received.active_difficulty);
		// Old node doesn't know about database_backend or new fields
		auto remaining = static_cast<uint16_t> (payload.size ()) - old_latest_size;
		ASSERT_EQ (remaining, lumex::messages::telemetry_data::size_v2 - lumex::messages::telemetry_data::size_v1);
		lumex::read (stream, old_received.unknown_data, remaining);
	}

	ASSERT_EQ (old_received.database_backend, lumex::messages::telemetry_database_backend::unknown);
	ASSERT_EQ (old_received.unknown_data.size (), lumex::messages::telemetry_data::size_v2 - lumex::messages::telemetry_data::size_v1);

	// Old node's validate_signature: serialize known fields + unknown_data
	std::vector<uint8_t> verify_bytes;
	{
		lumex::vectorstream stream (verify_bytes);
		lumex::write (stream, old_received.node_id);
		lumex::write (stream, boost::endian::native_to_big (old_received.block_count));
		lumex::write (stream, boost::endian::native_to_big (old_received.cemented_count));
		lumex::write (stream, boost::endian::native_to_big (old_received.unchecked_count));
		lumex::write (stream, boost::endian::native_to_big (old_received.account_count));
		lumex::write (stream, boost::endian::native_to_big (old_received.bandwidth_cap));
		lumex::write (stream, boost::endian::native_to_big (old_received.peer_count));
		lumex::write (stream, old_received.protocol_version);
		lumex::write (stream, boost::endian::native_to_big (old_received.uptime));
		lumex::write (stream, old_received.genesis_block.bytes);
		lumex::write (stream, old_received.major_version);
		lumex::write (stream, old_received.minor_version);
		lumex::write (stream, old_received.patch_version);
		lumex::write (stream, old_received.pre_release_version);
		lumex::write (stream, static_cast<uint8_t> (old_received.maker));
		lumex::write (stream, boost::endian::native_to_big (std::chrono::duration_cast<std::chrono::milliseconds> (old_received.timestamp.time_since_epoch ()).count ()));
		lumex::write (stream, boost::endian::native_to_big (old_received.active_difficulty));
		lumex::write (stream, old_received.unknown_data);
	}
	ASSERT_FALSE (lumex::validate_message (old_received.node_id, verify_bytes.data (), verify_bytes.size (), old_received.signature));
}

TEST (telemetry, no_peers)
{
	lumex::test::system system (1);

	auto responses = system.nodes[0]->telemetry.get_all_telemetries ();
	ASSERT_TRUE (responses.empty ());
}

TEST (telemetry, basic)
{
	lumex::test::system system;
	lumex::node_flags node_flags;
	auto node_client = system.add_node (node_flags);
	auto node_server = system.add_node (node_flags);

	// Request telemetry metrics
	auto channel = node_client->network.find_node_id (node_server->get_node_id ());
	ASSERT_NE (nullptr, channel);

	std::optional<lumex::messages::telemetry_data> telemetry_data;
	ASSERT_TIMELY (5s, telemetry_data = node_client->telemetry.get_telemetry (channel->get_remote_endpoint ()));
	ASSERT_EQ (node_server->get_node_id (), telemetry_data->node_id);

	// Check the metrics are correct
	ASSERT_TRUE (lumex::test::compare_telemetry (*telemetry_data, *node_server));

	// Call again straight away
	auto telemetry_data_2 = node_client->telemetry.get_telemetry (channel->get_remote_endpoint ());
	ASSERT_TRUE (telemetry_data_2);

	// Call again straight away
	auto telemetry_data_3 = node_client->telemetry.get_telemetry (channel->get_remote_endpoint ());
	ASSERT_TRUE (telemetry_data_3);

	// we expect at least one consecutive repeat of telemetry
	ASSERT_TRUE (*telemetry_data == telemetry_data_2 || telemetry_data_2 == telemetry_data_3);

	// Wait the cache period and check cache is not used
	WAIT (3s);

	std::optional<lumex::messages::telemetry_data> telemetry_data_4;
	ASSERT_TIMELY (5s, telemetry_data_4 = node_client->telemetry.get_telemetry (channel->get_remote_endpoint ()));
	ASSERT_NE (*telemetry_data, *telemetry_data_4);
}

TEST (telemetry, invalid_endpoint)
{
	lumex::test::system system (2);

	auto node_client = system.nodes.front ();
	auto node_server = system.nodes.back ();

	node_client->telemetry.trigger ();

	// Give some time for nodes to exchange telemetry
	WAIT (1s);

	lumex::endpoint endpoint = *lumex::parse_endpoint ("::ffff:240.0.0.0:12345");
	ASSERT_FALSE (node_client->telemetry.get_telemetry (endpoint));
}

TEST (telemetry, disconnected)
{
	lumex::test::system system;
	lumex::node_flags node_flags;
	auto node_client = system.add_node (node_flags);
	auto node_server = system.add_node (node_flags);

	auto channel = node_client->network.find_node_id (node_server->get_node_id ());
	ASSERT_NE (nullptr, channel);

	// Ensure telemetry is available before disconnecting
	ASSERT_TIMELY (5s, node_client->telemetry.get_telemetry (channel->get_remote_endpoint ()));

	system.stop_node (*node_server);
	ASSERT_TRUE (channel);

	// Ensure telemetry from disconnected peer is removed
	ASSERT_TIMELY (5s, !node_client->telemetry.get_telemetry (channel->get_remote_endpoint ()));
}

TEST (telemetry, dos_tcp)
{
	// Confirm that telemetry_reqs are not processed
	lumex::test::system system;
	lumex::node_flags node_flags;
	auto node_client = system.add_node (node_flags);
	auto node_server = system.add_node (node_flags);

	auto channel = node_client->network.tcp_channels.find_node_id (node_server->get_node_id ());
	ASSERT_NE (nullptr, channel);

	lumex::messages::telemetry_req message{ lumex::dev::network_params.network };
	for (int i = 0; i < 10; ++i)
	{
		channel->send (message, lumex::transport::traffic_type::test, [] (boost::system::error_code const & ec, size_t size_a) {
			ASSERT_FALSE (ec);
		});
	}

	// Should process telemetry_req messages
	ASSERT_TIMELY (5s, 1 < node_server->stats.count (lumex::stat::type::message, lumex::stat::detail::telemetry_req, lumex::stat::dir::in));

	// But not respond to all of them (by default there are 2 broadcasts per second in dev mode)
	ASSERT_ALWAYS (1s, node_server->stats.count (lumex::stat::type::message, lumex::stat::detail::telemetry_ack, lumex::stat::dir::out) < 7);
}

TEST (telemetry, disable_metrics)
{
	lumex::test::system system;
	lumex::node_flags node_flags;
	auto node_client = system.add_node (node_flags);
	node_flags.disable_providing_telemetry_metrics = true;
	auto node_server = system.add_node (node_flags);

	// Try and request metrics from a node which is turned off but a channel is not closed yet
	auto channel = node_client->network.find_node_id (node_server->get_node_id ());
	ASSERT_NE (nullptr, channel);

	node_client->telemetry.trigger ();

	ASSERT_NEVER (1s, node_client->telemetry.get_telemetry (channel->get_remote_endpoint ()));

	// It should still be able to receive metrics though
	auto channel1 = node_server->network.find_node_id (node_client->get_node_id ());
	ASSERT_NE (nullptr, channel1);

	std::optional<lumex::messages::telemetry_data> telemetry_data;
	ASSERT_TIMELY (5s, telemetry_data = node_server->telemetry.get_telemetry (channel1->get_remote_endpoint ()));

	ASSERT_TRUE (lumex::test::compare_telemetry (*telemetry_data, *node_client));
}

TEST (telemetry, max_possible_size)
{
	lumex::test::system system;
	lumex::node_flags node_flags;
	node_flags.disable_providing_telemetry_metrics = true;
	auto node_client = system.add_node (node_flags);
	auto node_server = system.add_node (node_flags);

	lumex::messages::telemetry_data data;
	data.unknown_data.resize (lumex::messages::message_header::telemetry_size_mask.to_ulong () - lumex::messages::telemetry_data::latest_size);

	lumex::messages::telemetry_ack message{ lumex::dev::network_params.network, data };

	auto channel = node_client->network.tcp_channels.find_node_id (node_server->get_node_id ());
	ASSERT_NE (nullptr, channel);
	channel->send (message, lumex::transport::traffic_type::test, [] (boost::system::error_code const & ec, size_t size_a) {
		ASSERT_FALSE (ec);
	});

	ASSERT_TIMELY_EQ (5s, 1, node_server->stats.count (lumex::stat::type::message, lumex::stat::detail::telemetry_ack, lumex::stat::dir::in));
}

TEST (telemetry, maker_pruning)
{
	lumex::test::system system;
	lumex::node_flags node_flags;
	auto node_client = system.add_node (node_flags);
	node_flags.enable_pruning = true;
	node_flags.disable_topo_index = true; // Topo index is incompatible with pruning
	lumex::node_config config;
	config.enable_voting = false;
	auto node_server = system.add_node (config, node_flags);

	// Request telemetry metrics
	auto channel = node_client->network.find_node_id (node_server->get_node_id ());
	ASSERT_NE (nullptr, channel);

	std::optional<lumex::messages::telemetry_data> telemetry_data;
	ASSERT_TIMELY (5s, telemetry_data = node_client->telemetry.get_telemetry (channel->get_remote_endpoint ()));
	ASSERT_EQ (node_server->get_node_id (), telemetry_data->node_id);

	// Ensure telemetry response indicates pruned node
	ASSERT_EQ (lumex::messages::telemetry_maker::nf_pruned_node, telemetry_data->maker);
}

TEST (telemetry, invalid_signature)
{
	lumex::test::system system;
	auto & node = *system.add_node ();

	auto telemetry = node.local_telemetry ();
	telemetry.block_count = 9999; // Change data so signature is no longer valid

	auto message = lumex::messages::telemetry_ack{ lumex::dev::network_params.network, telemetry };
	node.inbound (message, lumex::test::fake_channel (node));

	ASSERT_TIMELY (5s, node.stats.count (lumex::stat::type::telemetry, lumex::stat::detail::invalid_signature) > 0);
	ASSERT_ALWAYS (1s, node.stats.count (lumex::stat::type::telemetry, lumex::stat::detail::process) == 0)
}

TEST (telemetry, mismatched_node_id)
{
	lumex::test::system system;
	auto & node = *system.add_node ();

	auto telemetry = node.local_telemetry ();

	auto message = lumex::messages::telemetry_ack{ lumex::dev::network_params.network, telemetry };
	node.inbound (message, lumex::test::fake_channel (node, /* node id */ { 123 }));

	ASSERT_TIMELY (5s, node.stats.count (lumex::stat::type::telemetry, lumex::stat::detail::node_id_mismatch) > 0);
	ASSERT_ALWAYS (1s, node.stats.count (lumex::stat::type::telemetry, lumex::stat::detail::process) == 0)
}

TEST (telemetry, ongoing_broadcasts)
{
	lumex::test::system system;
	lumex::node_flags node_flags;
	auto & node1 = *system.add_node (node_flags);
	auto & node2 = *system.add_node (node_flags);

	ASSERT_TIMELY (5s, node1.stats.count (lumex::stat::type::telemetry, lumex::stat::detail::process) >= 3);
	ASSERT_TIMELY (5s, node2.stats.count (lumex::stat::type::telemetry, lumex::stat::detail::process) >= 3)
}
