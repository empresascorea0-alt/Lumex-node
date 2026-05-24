#include <lumex/crypto_lib/random_pool.hpp>
#include <lumex/lib/blockbuilders.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/stream.hpp>
#include <lumex/lib/vote.hpp>
#include <lumex/messages/messages.hpp>
#include <lumex/node/endpoint.hpp>
#include <lumex/node/network.hpp>
#include <lumex/secure/network_params.hpp>
#include <lumex/test_common/random.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/asio/ip/address_v6.hpp>

namespace
{
std::shared_ptr<lumex::block> random_block ()
{
	lumex::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (lumex::test::random_hash ())
				 .destination (lumex::keypair ().pub)
				 .balance (2)
				 .sign (lumex::keypair ().prv, 4)
				 .work (5)
				 .build ();
	return block;
}
}

TEST (message, header_version)
{
	// Simplest message type
	lumex::messages::keepalive original{ lumex::dev::network_params.network };

	// Serialize the original keepalive message
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream (bytes);
		original.serialize (stream);
	}

	// Deserialize the byte stream back to a message header
	lumex::bufferstream stream (bytes.data (), bytes.size ());
	bool error = false;
	lumex::messages::message_header header (error, stream);
	ASSERT_FALSE (error);

	// Check header versions
	ASSERT_EQ (lumex::dev::network_params.network.protocol_version_min, header.version_min);
	ASSERT_EQ (lumex::dev::network_params.network.protocol_version, header.version_using);
	ASSERT_EQ (lumex::dev::network_params.network.protocol_version, header.version_max);
	ASSERT_EQ (lumex::messages::message_type::keepalive, header.type);
}

TEST (message, keepalive_serialization)
{
	lumex::messages::keepalive request1{ lumex::dev::network_params.network };
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream (bytes);
		request1.serialize (stream);
	}
	auto error (false);
	lumex::bufferstream stream (bytes.data (), bytes.size ());
	lumex::messages::message_header header (error, stream);
	ASSERT_FALSE (error);
	lumex::messages::keepalive request2 (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (request1, request2);
}

TEST (message, keepalive_deserialize)
{
	lumex::messages::keepalive message1{ lumex::dev::network_params.network };
	message1.peers[0] = lumex::endpoint (boost::asio::ip::address_v6::loopback (), 10000);
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream (bytes);
		message1.serialize (stream);
	}
	lumex::bufferstream stream (bytes.data (), bytes.size ());
	auto error (false);
	lumex::messages::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (lumex::messages::message_type::keepalive, header.type);
	lumex::messages::keepalive message2 (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (message1.peers, message2.peers);
}

TEST (message, publish)
{
	// Create a random block
	auto block = random_block ();
	lumex::messages::publish original{ lumex::dev::network_params.network, block };
	ASSERT_FALSE (original.is_originator ());

	// Serialize the original publish message
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream (bytes);
		original.serialize (stream);
	}

	// Deserialize the byte stream back to a publish message
	lumex::bufferstream stream (bytes.data (), bytes.size ());
	bool error = false;
	lumex::messages::message_header header (error, stream);
	ASSERT_FALSE (error);
	lumex::messages::publish deserialized (error, stream, header);
	ASSERT_FALSE (error);

	// Assert that the original and deserialized messages are equal
	ASSERT_EQ (original, deserialized);
	ASSERT_EQ (*original.block, *deserialized.block);
	ASSERT_EQ (original.is_originator (), deserialized.is_originator ());
}

TEST (message, publish_originator_flag)
{
	// Create a random block
	auto block = random_block ();
	lumex::messages::publish original{ lumex::dev::network_params.network, block, /* originator */ true };
	ASSERT_TRUE (original.is_originator ());

	// Serialize the original publish message
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream (bytes);
		original.serialize (stream);
	}

	// Deserialize the byte stream back to a publish message
	lumex::bufferstream stream (bytes.data (), bytes.size ());
	bool error = false;
	lumex::messages::message_header header (error, stream);
	ASSERT_FALSE (error);
	lumex::messages::publish deserialized (error, stream, header);
	ASSERT_FALSE (error);

	// Assert that the originator flag is set correctly in both the original and deserialized messages
	ASSERT_TRUE (deserialized.is_originator ());
	ASSERT_EQ (original, deserialized);
	ASSERT_EQ (*original.block, *deserialized.block);
}

TEST (message, confirm_header_flags)
{
	lumex::messages::message_header header_v2{ lumex::dev::network_params.network, lumex::messages::message_type::confirm_req };
	header_v2.confirm_set_v2 (true);

	const uint8_t value = 0b0110'1001;

	header_v2.count_v2_set (value); // Max count value

	ASSERT_TRUE (header_v2.confirm_is_v2 ());
	ASSERT_EQ (header_v2.count_v2_get (), value);

	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream (bytes);
		header_v2.serialize (stream);
	}
	lumex::bufferstream stream (bytes.data (), bytes.size ());

	bool error = false;
	lumex::messages::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (lumex::messages::message_type::confirm_req, header.type);

	ASSERT_TRUE (header.confirm_is_v2 ());
	ASSERT_EQ (header.count_v2_get (), value);
}

TEST (message, confirm_header_flags_max)
{
	lumex::messages::message_header header_v2{ lumex::dev::network_params.network, lumex::messages::message_type::confirm_req };
	header_v2.confirm_set_v2 (true);
	header_v2.count_v2_set (255); // Max count value

	ASSERT_TRUE (header_v2.confirm_is_v2 ());
	ASSERT_EQ (header_v2.count_v2_get (), 255);

	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream (bytes);
		header_v2.serialize (stream);
	}
	lumex::bufferstream stream (bytes.data (), bytes.size ());

	bool error = false;
	lumex::messages::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (lumex::messages::message_type::confirm_req, header.type);

	ASSERT_TRUE (header.confirm_is_v2 ());
	ASSERT_EQ (header.count_v2_get (), 255);
}

TEST (message, confirm_ack_hash_serialization)
{
	std::vector<lumex::block_hash> hashes;
	for (auto i (hashes.size ()); i < 15; i++)
	{
		lumex::keypair key1;
		lumex::block_hash previous;
		lumex::random_pool::generate_block (previous.bytes.data (), previous.bytes.size ());
		lumex::block_builder builder;
		auto block = builder
					 .state ()
					 .account (key1.pub)
					 .previous (previous)
					 .representative (key1.pub)
					 .balance (2)
					 .link (4)
					 .sign (key1.prv, key1.pub)
					 .work (5)
					 .build ();
		hashes.push_back (block->hash ());
	}
	lumex::keypair representative1;
	auto vote = lumex::test::make_vote (representative1, { hashes }, 0, 0);
	lumex::messages::confirm_ack con1{ lumex::dev::network_params.network, vote };
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream1 (bytes);
		con1.serialize (stream1);
	}
	lumex::bufferstream stream2 (bytes.data (), bytes.size ());
	bool error (false);
	lumex::messages::message_header header (error, stream2);
	lumex::messages::confirm_ack con2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (con1, con2);
	ASSERT_EQ (hashes, con2.vote->hashes);
	ASSERT_FALSE (header.confirm_is_v2 ());
	ASSERT_EQ (header.count_get (), hashes.size ());
	ASSERT_FALSE (con2.is_rebroadcasted ());
}

TEST (message, confirm_ack_hash_serialization_v2)
{
	std::vector<lumex::block_hash> hashes;
	for (auto i (hashes.size ()); i < 255; i++)
	{
		lumex::keypair key1;
		lumex::block_hash previous;
		lumex::random_pool::generate_block (previous.bytes.data (), previous.bytes.size ());
		lumex::block_builder builder;
		auto block = builder
					 .state ()
					 .account (key1.pub)
					 .previous (previous)
					 .representative (key1.pub)
					 .balance (2)
					 .link (4)
					 .sign (key1.prv, key1.pub)
					 .work (5)
					 .build ();
		hashes.push_back (block->hash ());
	}

	lumex::keypair representative1;
	auto vote = lumex::test::make_vote (representative1, { hashes }, 0, 0);
	lumex::messages::confirm_ack con1{ lumex::dev::network_params.network, vote };
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream1 (bytes);
		con1.serialize (stream1);
	}
	lumex::bufferstream stream2 (bytes.data (), bytes.size ());
	bool error (false);
	lumex::messages::message_header header (error, stream2);
	lumex::messages::confirm_ack con2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (con1, con2);
	ASSERT_EQ (hashes, con2.vote->hashes);
	ASSERT_TRUE (header.confirm_is_v2 ());
	ASSERT_EQ (header.count_v2_get (), hashes.size ());
	ASSERT_FALSE (con2.is_rebroadcasted ());
}

TEST (message, confirm_ack_rebroadcasted_flag)
{
	lumex::keypair representative1;
	auto vote = lumex::test::make_vote (representative1, std::vector<lumex::block_hash> (), 0, 0);
	lumex::messages::confirm_ack con1{ lumex::dev::network_params.network, vote, /* rebroadcasted */ true };
	ASSERT_TRUE (con1.is_rebroadcasted ());
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream1 (bytes);
		con1.serialize (stream1);
	}
	lumex::bufferstream stream2 (bytes.data (), bytes.size ());
	bool error (false);
	lumex::messages::message_header header (error, stream2);
	lumex::messages::confirm_ack con2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (con1, con2);
	ASSERT_TRUE (con2.vote->hashes.empty ());
	ASSERT_TRUE (con2.is_rebroadcasted ());
}

TEST (message, confirm_req_hash_serialization)
{
	lumex::keypair key1;
	lumex::keypair key2;
	lumex::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (key2.pub)
				 .balance (200)
				 .sign (lumex::keypair ().prv, 2)
				 .work (3)
				 .build ();
	lumex::messages::confirm_req req{ lumex::dev::network_params.network, block->hash (), block->root () };
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream (bytes);
		req.serialize (stream);
	}
	auto error (false);
	lumex::bufferstream stream2 (bytes.data (), bytes.size ());
	lumex::messages::message_header header (error, stream2);
	lumex::messages::confirm_req req2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (req, req2);
	ASSERT_EQ (req.roots_hashes, req2.roots_hashes);
	ASSERT_EQ (header.count_get (), req.roots_hashes.size ());
}

TEST (message, confirm_req_hash_batch_serialization)
{
	lumex::keypair key;
	lumex::keypair representative;
	std::vector<std::pair<lumex::block_hash, lumex::root>> roots_hashes;
	lumex::block_builder builder;
	auto open = builder
				.state ()
				.account (key.pub)
				.previous (0)
				.representative (representative.pub)
				.balance (2)
				.link (4)
				.sign (key.prv, key.pub)
				.work (5)
				.build ();
	roots_hashes.push_back (std::make_pair (open->hash (), open->root ()));
	for (auto i (roots_hashes.size ()); i < 7; i++)
	{
		lumex::keypair key1;
		lumex::block_hash previous;
		lumex::random_pool::generate_block (previous.bytes.data (), previous.bytes.size ());
		auto block = builder
					 .state ()
					 .account (key1.pub)
					 .previous (previous)
					 .representative (representative.pub)
					 .balance (2)
					 .link (4)
					 .sign (key1.prv, key1.pub)
					 .work (5)
					 .build ();
		roots_hashes.push_back (std::make_pair (block->hash (), block->root ()));
	}
	roots_hashes.push_back (std::make_pair (open->hash (), open->root ()));
	lumex::messages::confirm_req req{ lumex::dev::network_params.network, roots_hashes };
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream (bytes);
		req.serialize (stream);
	}
	auto error (false);
	lumex::bufferstream stream2 (bytes.data (), bytes.size ());
	lumex::messages::message_header header (error, stream2);
	lumex::messages::confirm_req req2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (req, req2);
	ASSERT_EQ (req.roots_hashes, req2.roots_hashes);
	ASSERT_EQ (req.roots_hashes, roots_hashes);
	ASSERT_EQ (req2.roots_hashes, roots_hashes);
	ASSERT_EQ (header.count_get (), req.roots_hashes.size ());
	ASSERT_FALSE (header.confirm_is_v2 ());
}

TEST (message, confirm_req_hash_batch_serialization_v2)
{
	lumex::keypair key;
	lumex::keypair representative;
	lumex::block_builder builder;
	auto open = builder
				.state ()
				.account (key.pub)
				.previous (0)
				.representative (representative.pub)
				.balance (2)
				.link (4)
				.sign (key.prv, key.pub)
				.work (5)
				.build ();

	std::vector<std::pair<lumex::block_hash, lumex::root>> roots_hashes;
	roots_hashes.push_back (std::make_pair (open->hash (), open->root ()));
	for (auto i (roots_hashes.size ()); i < 255; i++)
	{
		lumex::keypair key1;
		lumex::block_hash previous;
		lumex::random_pool::generate_block (previous.bytes.data (), previous.bytes.size ());
		auto block = builder
					 .state ()
					 .account (key1.pub)
					 .previous (previous)
					 .representative (representative.pub)
					 .balance (2)
					 .link (4)
					 .sign (key1.prv, key1.pub)
					 .work (5)
					 .build ();
		roots_hashes.push_back (std::make_pair (block->hash (), block->root ()));
	}

	lumex::messages::confirm_req req{ lumex::dev::network_params.network, roots_hashes };
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream (bytes);
		req.serialize (stream);
	}
	auto error (false);
	lumex::bufferstream stream2 (bytes.data (), bytes.size ());
	lumex::messages::message_header header (error, stream2);
	lumex::messages::confirm_req req2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (req, req2);
	ASSERT_EQ (req.roots_hashes, req2.roots_hashes);
	ASSERT_EQ (req.roots_hashes, roots_hashes);
	ASSERT_EQ (req2.roots_hashes, roots_hashes);
	ASSERT_EQ (header.count_v2_get (), req.roots_hashes.size ());
	ASSERT_TRUE (header.confirm_is_v2 ());
}

/**
 * Test that a confirm_ack can encode an empty hash set
 */
TEST (confirm_ack, empty_vote_hashes)
{
	lumex::keypair key;
	auto vote = std::make_shared<lumex::vote> (key.pub, key.prv, 0, 0, std::vector<lumex::block_hash>{} /* empty */);
	lumex::messages::confirm_ack message{ lumex::dev::network_params.network, vote };
}

TEST (message, bulk_pull_serialization)
{
	lumex::messages::bulk_pull message_in{ lumex::dev::network_params.network };
	message_in.header.flag_set (lumex::messages::message_header::bulk_pull_ascending_flag);
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream{ bytes };
		message_in.serialize (stream);
	}
	lumex::bufferstream stream{ bytes.data (), bytes.size () };
	bool error = false;
	lumex::messages::message_header header{ error, stream };
	ASSERT_FALSE (error);
	lumex::messages::bulk_pull message_out{ error, stream, header };
	ASSERT_FALSE (error);
	ASSERT_TRUE (header.bulk_pull_ascending ());
}

TEST (message, asc_pull_req_serialization_blocks)
{
	lumex::messages::asc_pull_req original{ lumex::dev::network_params.network };
	original.id = 7;
	original.type = lumex::messages::asc_pull_type::blocks;

	lumex::messages::asc_pull_req::blocks_payload original_payload{};
	original_payload.start = lumex::test::random_hash ();
	original_payload.count = 111;

	original.payload = original_payload;
	original.update_header ();

	// Serialize
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream{ bytes };
		original.serialize (stream);
	}
	lumex::bufferstream stream{ bytes.data (), bytes.size () };

	// Header
	bool error = false;
	lumex::messages::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (lumex::messages::message_type::asc_pull_req, header.type);

	// Message
	lumex::messages::asc_pull_req message (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (original.id, message.id);
	ASSERT_EQ (original.type, message.type);

	lumex::messages::asc_pull_req::blocks_payload message_payload;
	ASSERT_NO_THROW (message_payload = std::get<lumex::messages::asc_pull_req::blocks_payload> (message.payload));
	ASSERT_EQ (original_payload.start, message_payload.start);
	ASSERT_EQ (original_payload.count, message_payload.count);

	ASSERT_TRUE (lumex::at_end (stream));
}

TEST (message, asc_pull_req_serialization_account_info)
{
	lumex::messages::asc_pull_req original{ lumex::dev::network_params.network };
	original.id = 7;
	original.type = lumex::messages::asc_pull_type::account_info;

	lumex::messages::asc_pull_req::account_info_payload original_payload{};
	original_payload.target = lumex::test::random_hash ();

	original.payload = original_payload;
	original.update_header ();

	// Serialize
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream{ bytes };
		original.serialize (stream);
	}
	lumex::bufferstream stream{ bytes.data (), bytes.size () };

	// Header
	bool error = false;
	lumex::messages::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (lumex::messages::message_type::asc_pull_req, header.type);

	// Message
	lumex::messages::asc_pull_req message (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (original.id, message.id);
	ASSERT_EQ (original.type, message.type);

	lumex::messages::asc_pull_req::account_info_payload message_payload;
	ASSERT_NO_THROW (message_payload = std::get<lumex::messages::asc_pull_req::account_info_payload> (message.payload));
	ASSERT_EQ (original_payload.target, message_payload.target);

	ASSERT_TRUE (lumex::at_end (stream));
}

TEST (message, asc_pull_req_serialization_frontiers)
{
	lumex::messages::asc_pull_req original{ lumex::dev::network_params.network };
	original.id = 7;
	original.type = lumex::messages::asc_pull_type::frontiers;

	lumex::messages::asc_pull_req::frontiers_payload original_payload{};
	original_payload.start = lumex::test::random_account ();
	original_payload.count = 123;

	original.payload = original_payload;
	original.update_header ();

	// Serialize
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream{ bytes };
		original.serialize (stream);
	}
	lumex::bufferstream stream{ bytes.data (), bytes.size () };

	// Header
	bool error = false;
	lumex::messages::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (lumex::messages::message_type::asc_pull_req, header.type);

	// Message
	lumex::messages::asc_pull_req message (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (original.id, message.id);
	ASSERT_EQ (original.type, message.type);

	lumex::messages::asc_pull_req::frontiers_payload message_payload;
	ASSERT_NO_THROW (message_payload = std::get<lumex::messages::asc_pull_req::frontiers_payload> (message.payload));
	ASSERT_EQ (original_payload.start, message_payload.start);
	ASSERT_EQ (original_payload.count, message_payload.count);

	ASSERT_TRUE (lumex::at_end (stream));
}

TEST (message, asc_pull_ack_serialization_blocks)
{
	lumex::messages::asc_pull_ack original{ lumex::dev::network_params.network };
	original.id = 11;
	original.type = lumex::messages::asc_pull_type::blocks;

	lumex::messages::asc_pull_ack::blocks_payload original_payload{};
	for (int n = 0; n < lumex::messages::asc_pull_ack::blocks_payload::max_blocks; ++n)
	{
		original_payload.blocks.push_back (random_block ());
	}

	original.payload = original_payload;
	original.update_header ();

	// Serialize
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream{ bytes };
		original.serialize (stream);
	}
	lumex::bufferstream stream{ bytes.data (), bytes.size () };

	// Header
	bool error = false;
	lumex::messages::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (lumex::messages::message_type::asc_pull_ack, header.type);

	// Message
	lumex::messages::asc_pull_ack message (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (original.id, message.id);
	ASSERT_EQ (original.type, message.type);

	lumex::messages::asc_pull_ack::blocks_payload message_payload;
	ASSERT_NO_THROW (message_payload = std::get<lumex::messages::asc_pull_ack::blocks_payload> (message.payload));

	// Compare blocks
	ASSERT_EQ (original_payload.blocks.size (), message_payload.blocks.size ());
	ASSERT_TRUE (std::equal (original_payload.blocks.begin (), original_payload.blocks.end (), message_payload.blocks.begin (), message_payload.blocks.end (), [] (auto a, auto b) {
		return *a == *b;
	}));

	ASSERT_TRUE (lumex::at_end (stream));
}

TEST (message, asc_pull_ack_serialization_account_info)
{
	lumex::messages::asc_pull_ack original{ lumex::dev::network_params.network };
	original.id = 11;
	original.type = lumex::messages::asc_pull_type::account_info;

	lumex::messages::asc_pull_ack::account_info_payload original_payload{};
	original_payload.account = lumex::test::random_account ();
	original_payload.account_open = lumex::test::random_hash ();
	original_payload.account_head = lumex::test::random_hash ();
	original_payload.account_block_count = 932932132;
	original_payload.account_conf_frontier = lumex::test::random_hash ();
	original_payload.account_conf_height = 847312;

	original.payload = original_payload;
	original.update_header ();

	// Serialize
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream{ bytes };
		original.serialize (stream);
	}
	lumex::bufferstream stream{ bytes.data (), bytes.size () };

	// Header
	bool error = false;
	lumex::messages::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (lumex::messages::message_type::asc_pull_ack, header.type);

	// Message
	lumex::messages::asc_pull_ack message (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (original.id, message.id);
	ASSERT_EQ (original.type, message.type);

	lumex::messages::asc_pull_ack::account_info_payload message_payload;
	ASSERT_NO_THROW (message_payload = std::get<lumex::messages::asc_pull_ack::account_info_payload> (message.payload));

	ASSERT_EQ (original_payload.account, message_payload.account);
	ASSERT_EQ (original_payload.account_open, message_payload.account_open);
	ASSERT_EQ (original_payload.account_head, message_payload.account_head);
	ASSERT_EQ (original_payload.account_block_count, message_payload.account_block_count);
	ASSERT_EQ (original_payload.account_conf_frontier, message_payload.account_conf_frontier);
	ASSERT_EQ (original_payload.account_conf_height, message_payload.account_conf_height);

	ASSERT_TRUE (lumex::at_end (stream));
}

TEST (message, asc_pull_ack_serialization_frontiers)
{
	lumex::messages::asc_pull_ack original{ lumex::dev::network_params.network };
	original.id = 11;
	original.type = lumex::messages::asc_pull_type::frontiers;

	lumex::messages::asc_pull_ack::frontiers_payload original_payload{};
	for (int n = 0; n < lumex::messages::asc_pull_ack::frontiers_payload::max_frontiers; ++n)
	{
		original_payload.frontiers.push_back ({ lumex::test::random_account (), lumex::test::random_hash () });
	}

	original.payload = original_payload;
	original.update_header ();

	// Serialize
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream{ bytes };
		original.serialize (stream);
	}
	lumex::bufferstream stream{ bytes.data (), bytes.size () };

	// Header
	bool error = false;
	lumex::messages::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (lumex::messages::message_type::asc_pull_ack, header.type);

	// Message
	lumex::messages::asc_pull_ack message (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (original.id, message.id);
	ASSERT_EQ (original.type, message.type);

	lumex::messages::asc_pull_ack::frontiers_payload message_payload;
	ASSERT_NO_THROW (message_payload = std::get<lumex::messages::asc_pull_ack::frontiers_payload> (message.payload));

	ASSERT_EQ (original_payload.frontiers, message_payload.frontiers);

	ASSERT_TRUE (lumex::at_end (stream));
}

TEST (message, node_id_handshake_query_serialization)
{
	lumex::messages::node_id_handshake::query_payload query{};
	query.cookie = 7;
	lumex::messages::node_id_handshake original{ lumex::dev::network_params.network, query };

	// Serialize
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream{ bytes };
		original.serialize (stream);
	}
	lumex::bufferstream stream{ bytes.data (), bytes.size () };

	// Header
	bool error = false;
	lumex::messages::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (lumex::messages::message_type::node_id_handshake, header.type);

	// Message
	lumex::messages::node_id_handshake message{ error, stream, header };
	ASSERT_FALSE (error);
	ASSERT_TRUE (message.query);
	ASSERT_FALSE (message.response);

	ASSERT_EQ (original.query->cookie, message.query->cookie);

	ASSERT_TRUE (lumex::at_end (stream));
}

TEST (message, node_id_handshake_response_serialization)
{
	lumex::messages::node_id_handshake::response_payload response{};
	response.node_id = lumex::account{ 7 };
	response.signature = lumex::signature{ 11 };
	lumex::messages::node_id_handshake original{ lumex::dev::network_params.network, std::nullopt, response };

	// Serialize
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream{ bytes };
		original.serialize (stream);
	}
	lumex::bufferstream stream{ bytes.data (), bytes.size () };

	// Header
	bool error = false;
	lumex::messages::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (lumex::messages::message_type::node_id_handshake, header.type);

	// Message
	lumex::messages::node_id_handshake message{ error, stream, header };
	ASSERT_FALSE (error);
	ASSERT_FALSE (message.query);
	ASSERT_TRUE (message.response);
	ASSERT_TRUE (std::holds_alternative<std::monostate> (message.response->ext));

	ASSERT_EQ (original.response->node_id, message.response->node_id);
	ASSERT_EQ (original.response->signature, message.response->signature);

	ASSERT_TRUE (lumex::at_end (stream));
}

TEST (message, node_id_handshake_response_v2_serialization)
{
	lumex::messages::node_id_handshake::response_payload response{};
	response.node_id = lumex::account{ 7 };
	response.signature = lumex::signature{ 11 };
	lumex::messages::node_id_handshake::response_payload::v2_payload v2_pld{};
	v2_pld.salt = 17;
	v2_pld.genesis = lumex::block_hash{ 13 };
	response.ext = v2_pld;

	lumex::messages::node_id_handshake original{ lumex::dev::network_params.network, std::nullopt, response };

	// Serialize
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream{ bytes };
		original.serialize (stream);
	}
	lumex::bufferstream stream{ bytes.data (), bytes.size () };

	// Header
	bool error = false;
	lumex::messages::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (lumex::messages::message_type::node_id_handshake, header.type);

	// Message
	lumex::messages::node_id_handshake message{ error, stream, header };
	ASSERT_FALSE (error);
	ASSERT_FALSE (message.query);
	ASSERT_TRUE (message.response);

	auto * v2_orig = std::get_if<lumex::messages::node_id_handshake::response_payload::v2_payload> (&original.response->ext);
	auto * v2_msg = std::get_if<lumex::messages::node_id_handshake::response_payload::v2_payload> (&message.response->ext);
	ASSERT_TRUE (v2_orig);
	ASSERT_TRUE (v2_msg);

	ASSERT_EQ (original.response->node_id, message.response->node_id);
	ASSERT_EQ (original.response->signature, message.response->signature);
	ASSERT_EQ (v2_orig->salt, v2_msg->salt);
	ASSERT_EQ (v2_orig->genesis, v2_msg->genesis);

	ASSERT_TRUE (lumex::at_end (stream));
}

TEST (message, node_id_handshake_response_v3_serialization)
{
	lumex::messages::node_id_handshake::response_payload response{};
	response.node_id = lumex::account{ 7 };
	response.signature = lumex::signature{ 11 };
	lumex::messages::node_id_handshake::response_payload::v3_payload v3_pld{};
	v3_pld.salt = 17;
	v3_pld.genesis = lumex::block_hash{ 13 };
	v3_pld.flags = lumex::node_capabilities_flags{ lumex::node_capabilities::topo_index } | lumex::node_capabilities::vote_storage;
	response.ext = v3_pld;

	lumex::messages::node_id_handshake original{ lumex::dev::network_params.network, std::nullopt, response };

	// Serialize
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream{ bytes };
		original.serialize (stream);
	}
	lumex::bufferstream stream{ bytes.data (), bytes.size () };

	// Header
	bool error = false;
	lumex::messages::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (lumex::messages::message_type::node_id_handshake, header.type);

	// Message
	lumex::messages::node_id_handshake message{ error, stream, header };
	ASSERT_FALSE (error);
	ASSERT_FALSE (message.query);
	ASSERT_TRUE (message.response);

	auto * v3_orig = std::get_if<lumex::messages::node_id_handshake::response_payload::v3_payload> (&original.response->ext);
	auto * v3_msg = std::get_if<lumex::messages::node_id_handshake::response_payload::v3_payload> (&message.response->ext);
	ASSERT_TRUE (v3_orig);
	ASSERT_TRUE (v3_msg);

	ASSERT_EQ (original.response->node_id, message.response->node_id);
	ASSERT_EQ (original.response->signature, message.response->signature);
	ASSERT_EQ (v3_orig->salt, v3_msg->salt);
	ASSERT_EQ (v3_orig->genesis, v3_msg->genesis);
	ASSERT_EQ (v3_orig->flags, v3_msg->flags);

	ASSERT_TRUE (lumex::at_end (stream));
}

TEST (handshake, signature)
{
	lumex::keypair node_id{};
	lumex::keypair node_id_2{};
	auto cookie = lumex::random_pool::generate<lumex::uint256_union> ();
	auto cookie_2 = lumex::random_pool::generate<lumex::uint256_union> ();

	lumex::messages::node_id_handshake::response_payload response{};
	response.node_id = node_id.pub;
	response.sign (cookie, node_id);
	ASSERT_TRUE (response.validate (cookie));

	// Invalid cookie
	ASSERT_FALSE (response.validate (cookie_2));

	// Invalid node id
	response.node_id = node_id_2.pub;
	ASSERT_FALSE (response.validate (cookie));
}

TEST (handshake, signature_v2)
{
	lumex::keypair node_id{};
	lumex::keypair node_id_2{};
	auto cookie = lumex::random_pool::generate<lumex::uint256_union> ();
	auto cookie_2 = lumex::random_pool::generate<lumex::uint256_union> ();

	lumex::messages::node_id_handshake::response_payload original{};
	original.node_id = node_id.pub;
	lumex::messages::node_id_handshake::response_payload::v2_payload v2{};
	v2.genesis = lumex::test::random_hash ();
	v2.salt = lumex::random_pool::generate<lumex::uint256_union> ();
	original.ext = v2;
	original.sign (cookie, node_id);
	ASSERT_TRUE (original.validate (cookie));

	// Invalid cookie
	ASSERT_FALSE (original.validate (cookie_2));

	// Invalid node id
	{
		auto message = original;
		ASSERT_TRUE (message.validate (cookie));
		message.node_id = node_id_2.pub;
		ASSERT_FALSE (message.validate (cookie));
	}

	// Invalid genesis
	{
		auto message = original;
		ASSERT_TRUE (message.validate (cookie));
		std::get<lumex::messages::node_id_handshake::response_payload::v2_payload> (message.ext).genesis = lumex::test::random_hash ();
		ASSERT_FALSE (message.validate (cookie));
	}

	// Invalid salt
	{
		auto message = original;
		ASSERT_TRUE (message.validate (cookie));
		std::get<lumex::messages::node_id_handshake::response_payload::v2_payload> (message.ext).salt = lumex::random_pool::generate<lumex::uint256_union> ();
		ASSERT_FALSE (message.validate (cookie));
	}
}

TEST (handshake, signature_v3)
{
	lumex::keypair node_id{};
	auto cookie = lumex::random_pool::generate<lumex::uint256_union> ();

	lumex::messages::node_id_handshake::response_payload original{};
	original.node_id = node_id.pub;
	lumex::messages::node_id_handshake::response_payload::v3_payload v3{};
	v3.genesis = lumex::test::random_hash ();
	v3.salt = lumex::random_pool::generate<lumex::uint256_union> ();
	v3.flags = lumex::node_capabilities::topo_index;
	original.ext = v3;
	original.sign (cookie, node_id);
	ASSERT_TRUE (original.validate (cookie));

	// Mutate flags -> signature should fail
	{
		auto message = original;
		ASSERT_TRUE (message.validate (cookie));
		std::get<lumex::messages::node_id_handshake::response_payload::v3_payload> (message.ext).flags = {};
		ASSERT_FALSE (message.validate (cookie));
	}

	// Mutate genesis -> signature should fail
	{
		auto message = original;
		ASSERT_TRUE (message.validate (cookie));
		std::get<lumex::messages::node_id_handshake::response_payload::v3_payload> (message.ext).genesis = lumex::test::random_hash ();
		ASSERT_FALSE (message.validate (cookie));
	}

	// Mutate reserved -> signature should fail
	{
		auto message = original;
		ASSERT_TRUE (message.validate (cookie));
		std::get<lumex::messages::node_id_handshake::response_payload::v3_payload> (message.ext).reserved = 42;
		ASSERT_FALSE (message.validate (cookie));
	}
}
