#include <lumex/lib/blockbuilders.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/lib/vote.hpp>
#include <lumex/messages/messages.hpp>
#include <lumex/node/transport/message_deserializer.hpp>
#include <lumex/test_common/random.hpp>
#include <lumex/test_common/system.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

// Test the successful cases for message_deserializer, checking the supported message types and
// the integrity of the deserialized outcome.
template <class message_type>
auto message_deserializer_success_checker (message_type & message_original) -> void
{
	// Dependencies for the message deserializer.
	lumex::network_filter filter (1);
	lumex::block_uniquer block_uniquer;
	lumex::vote_uniquer vote_uniquer;

	// Data used to simulate the incoming buffer to be deserialized, the offset tracks how much has been read from the input_source
	// as the read function is called first to read the header, then called again to read the payload.
	std::vector<uint8_t> input_source;
	std::size_t offset{ 0 };

	// Message Deserializer with the query function tweaked to read from the `input_source`.
	auto const message_deserializer = std::make_shared<lumex::transport::message_deserializer> (lumex::dev::network_params.network, filter, block_uniquer, vote_uniquer,
	[&input_source, &offset] (std::shared_ptr<std::vector<uint8_t>> const & data_a, std::size_t size_a, std::function<void (boost::system::error_code const &, std::size_t)> callback_a) {
		debug_assert (input_source.size () >= size_a);
		data_a->resize (size_a);
		auto const copy_start = input_source.begin () + offset;
		std::copy (copy_start, copy_start + size_a, data_a->data ());
		offset += size_a;
		callback_a (boost::system::errc::make_error_code (boost::system::errc::success), size_a);
	});

	// Generating the values for the `input_source`.
	{
		lumex::vectorstream stream (input_source);
		message_original.serialize (stream);
	}

	// Deserializing and testing the success path.
	message_deserializer->read (
	[&message_original] (boost::system::error_code ec_a, std::unique_ptr<lumex::messages::message> message_a) {
		auto deserialized_message = dynamic_cast<message_type *> (message_a.get ());
		// Ensure the message type is supported.
		ASSERT_NE (deserialized_message, nullptr);
		auto deserialized_bytes = deserialized_message->to_bytes ();
		auto original_bytes = message_original.to_bytes ();
		// Ensure the integrity of the deserialized message.
		ASSERT_EQ (*deserialized_bytes, *original_bytes);
	});
	// This is a sanity test, to ensure the successful deserialization case passes.
	ASSERT_EQ (message_deserializer->status, lumex::transport::parse_status::success);
}

TEST (message_deserializer, exact_confirm_ack)
{
	lumex::test::system system{ 1 };
	lumex::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (1)
				 .balance (2)
				 .sign (lumex::keypair ().prv, 4)
				 .work (*system.work.generate (lumex::root (1)))
				 .build ();
	auto vote (std::make_shared<lumex::vote> (0, lumex::keypair ().prv, 0, 0, std::vector<lumex::block_hash>{ block->hash () }));
	lumex::messages::confirm_ack message{ lumex::dev::network_params.network, vote };

	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_confirm_req_hash)
{
	lumex::test::system system{ 1 };
	lumex::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (1)
				 .balance (2)
				 .sign (lumex::keypair ().prv, 4)
				 .work (*system.work.generate (lumex::root (1)))
				 .build ();
	// This test differs from the previous `exact_confirm_req` because this tests the confirm_req created from the block hash.
	lumex::messages::confirm_req message{ lumex::dev::network_params.network, block->hash (), block->root () };

	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_publish)
{
	lumex::test::system system{ 1 };
	lumex::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (1)
				 .balance (2)
				 .sign (lumex::keypair ().prv, 4)
				 .work (*system.work.generate (lumex::root (1)))
				 .build ();
	lumex::messages::publish message{ lumex::dev::network_params.network, block };

	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_keepalive)
{
	lumex::messages::keepalive message{ lumex::dev::network_params.network };

	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_frontier_req)
{
	lumex::messages::frontier_req message{ lumex::dev::network_params.network };
	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_telemetry_req)
{
	lumex::messages::telemetry_req message{ lumex::dev::network_params.network };
	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_telemetry_ack)
{
	lumex::messages::telemetry_data data;
	data.unknown_data.push_back (0xFF);

	lumex::messages::telemetry_ack message{ lumex::dev::network_params.network, data };
	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_bulk_pull)
{
	lumex::messages::bulk_pull message{ lumex::dev::network_params.network };
	message.header.flag_set (lumex::messages::message_header::bulk_pull_ascending_flag);

	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_bulk_pull_account)
{
	lumex::messages::bulk_pull_account message{ lumex::dev::network_params.network };
	message.flags = lumex::messages::bulk_pull_account_flags::pending_address_only;

	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_bulk_push)
{
	lumex::messages::bulk_push message{ lumex::dev::network_params.network };
	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_node_id_handshake)
{
	lumex::messages::node_id_handshake message{ lumex::dev::network_params.network, std::nullopt, std::nullopt };
	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_asc_pull_req)
{
	lumex::messages::asc_pull_req message{ lumex::dev::network_params.network };

	// The asc_pull_req checks for the message fields and the payload to be filled.
	message.id = 7;
	message.type = lumex::messages::asc_pull_type::account_info;

	lumex::messages::asc_pull_req::account_info_payload message_payload;
	message_payload.target = lumex::test::random_account ();
	message_payload.target_type = lumex::messages::asc_pull_req::hash_type::account;

	message.payload = message_payload;
	message.update_header ();

	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_asc_pull_ack)
{
	lumex::messages::asc_pull_ack message{ lumex::dev::network_params.network };

	// The asc_pull_ack checks for the message fields and the payload to be filled.
	message.id = 11;
	message.type = lumex::messages::asc_pull_type::account_info;

	lumex::messages::asc_pull_ack::account_info_payload message_payload;
	message_payload.account = lumex::test::random_account ();
	message_payload.account_open = lumex::test::random_hash ();
	message_payload.account_head = lumex::test::random_hash ();
	message_payload.account_block_count = 932932132;
	message_payload.account_conf_frontier = lumex::test::random_hash ();
	message_payload.account_conf_height = 847312;

	message.payload = message_payload;
	message.update_header ();

	message_deserializer_success_checker<decltype (message)> (message);
}
