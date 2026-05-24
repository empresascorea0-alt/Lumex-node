#include <lumex/lib/blockbuilders.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/network_filter.hpp>
#include <lumex/lib/ratios.hpp>
#include <lumex/lib/stream.hpp>
#include <lumex/messages/messages.hpp>
#include <lumex/node/endpoint.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/network_params.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

TEST (network_filter, apply)
{
	lumex::network_filter filter (4);
	ASSERT_FALSE (filter.check (34));
	ASSERT_FALSE (filter.apply (34));
	ASSERT_TRUE (filter.check (34));
	ASSERT_TRUE (filter.apply (34));
	filter.clear (lumex::network_filter::digest_t{ 34 });
	ASSERT_FALSE (filter.check (34));
	ASSERT_FALSE (filter.apply (34));
}

TEST (network_filter, unit)
{
	lumex::network_filter filter (1);
	auto one_block = [&filter] (std::shared_ptr<lumex::block> const & block_a, bool expect_duplicate_a) {
		lumex::messages::publish message{ lumex::dev::network_params.network, block_a };
		auto bytes (message.to_bytes ());
		lumex::bufferstream stream (bytes->data (), bytes->size ());

		// First read the header
		bool error{ false };
		lumex::messages::message_header header (error, stream);
		ASSERT_FALSE (error);

		// This validates lumex::messages::message_header::size
		ASSERT_EQ (bytes->size (), block_a->size (block_a->type ()) + header.size);

		// Now filter the rest of the stream
		bool duplicate (filter.apply (bytes->data (), bytes->size () - header.size));
		ASSERT_EQ (expect_duplicate_a, duplicate);

		// Make sure the stream was rewinded correctly
		auto block (lumex::deserialize_block (stream, header.block_type ()));
		ASSERT_NE (nullptr, block);
		ASSERT_EQ (*block, *block_a);
	};
	one_block (lumex::dev::genesis, false);
	for (int i = 0; i < 10; ++i)
	{
		one_block (lumex::dev::genesis, true);
	}
	lumex::state_block_builder builder;
	auto new_block = builder
					 .account (lumex::dev::genesis_key.pub)
					 .previous (lumex::dev::genesis->hash ())
					 .representative (lumex::dev::genesis_key.pub)
					 .balance (lumex::dev::constants.genesis_amount - 1000 * lumex::raw_ratio)
					 .link (lumex::public_key ())
					 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					 .work (0)
					 .build ();

	one_block (new_block, false);
	for (int i = 0; i < 10; ++i)
	{
		one_block (new_block, true);
	}
	for (int i = 0; i < 100; ++i)
	{
		one_block (lumex::dev::genesis, false);
		one_block (new_block, false);
	}
}

TEST (network_filter, many)
{
	lumex::network_filter filter (4);
	lumex::keypair key1;
	for (int i = 0; i < 100; ++i)
	{
		lumex::state_block_builder builder;
		auto block = builder
					 .account (lumex::dev::genesis_key.pub)
					 .previous (lumex::dev::genesis->hash ())
					 .representative (lumex::dev::genesis_key.pub)
					 .balance (lumex::dev::constants.genesis_amount - i * 1000 * lumex::raw_ratio)
					 .link (key1.pub)
					 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					 .work (0)
					 .build ();

		lumex::messages::publish message{ lumex::dev::network_params.network, block };
		auto bytes (message.to_bytes ());
		lumex::bufferstream stream (bytes->data (), bytes->size ());

		// First read the header
		bool error{ false };
		lumex::messages::message_header header (error, stream);
		ASSERT_FALSE (error);

		// This validates lumex::messages::message_header::size
		ASSERT_EQ (bytes->size (), block->size + header.size);

		// Now filter the rest of the stream
		// All blocks should pass through
		ASSERT_FALSE (filter.apply (bytes->data (), block->size));
		ASSERT_TRUE (filter.check (bytes->data (), block->size));
		ASSERT_FALSE (error);

		// Make sure the stream was rewinded correctly
		auto deserialized_block (lumex::deserialize_block (stream, header.block_type ()));
		ASSERT_NE (nullptr, deserialized_block);
		ASSERT_EQ (*block, *deserialized_block);
	}
}

TEST (network_filter, clear)
{
	lumex::network_filter filter (1);
	std::vector<uint8_t> bytes1{ 1, 2, 3 };
	std::vector<uint8_t> bytes2{ 1 };
	ASSERT_FALSE (filter.apply (bytes1.data (), bytes1.size ()));
	ASSERT_TRUE (filter.apply (bytes1.data (), bytes1.size ()));
	filter.clear (bytes1.data (), bytes1.size ());
	ASSERT_FALSE (filter.apply (bytes1.data (), bytes1.size ()));
	ASSERT_TRUE (filter.apply (bytes1.data (), bytes1.size ()));
	filter.clear (bytes2.data (), bytes2.size ());
	ASSERT_TRUE (filter.apply (bytes1.data (), bytes1.size ()));
	ASSERT_FALSE (filter.apply (bytes2.data (), bytes2.size ()));
}

TEST (network_filter, optional_digest)
{
	lumex::network_filter filter (1);
	std::vector<uint8_t> bytes1{ 1, 2, 3 };
	lumex::uint128_t digest{ 0 };
	ASSERT_FALSE (filter.apply (bytes1.data (), bytes1.size (), &digest));
	ASSERT_NE (0, digest);
	ASSERT_TRUE (filter.apply (bytes1.data (), bytes1.size ()));
	filter.clear (digest);
	ASSERT_FALSE (filter.apply (bytes1.data (), bytes1.size ()));
}

TEST (network_filter, expire)
{
	// Expire entries older than 2 epochs
	lumex::network_filter filter{ 4, 2 };

	ASSERT_FALSE (filter.apply (1)); // Entry with epoch 0
	filter.update (); // Bump epoch to 1
	ASSERT_FALSE (filter.apply (2)); // Entry with epoch 1

	// Both values should be detected as present
	ASSERT_TRUE (filter.check (1));
	ASSERT_TRUE (filter.check (2));

	filter.update (2); // Bump epoch to 3

	ASSERT_FALSE (filter.check (1)); // Entry with epoch 0 should be expired
	ASSERT_TRUE (filter.check (2)); // Entry with epoch 1 should still be present

	filter.update (); // Bump epoch to 4

	ASSERT_FALSE (filter.check (2)); // Entry with epoch 1 should be expired
	ASSERT_FALSE (filter.apply (2)); // Entry with epoch 1 should be replaced
}
