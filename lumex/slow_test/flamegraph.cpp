#include <lumex/lib/blockbuilders.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/test_common/system.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <chrono>

using namespace std::chrono_literals;

namespace
{
std::deque<lumex::keypair> rep_set (size_t count)
{
	std::deque<lumex::keypair> result;
	for (auto i = 0; i < count; ++i)
	{
		result.emplace_back (lumex::keypair{});
	}
	return result;
}
}

TEST (flamegraph, large_direct_processing)
{
	auto reps = rep_set (4);
	auto circulating = 10 * lumex::Klumex_ratio;
	lumex::test::system system;
	system.ledger_initialization_set (reps, circulating);
	auto & node = *system.add_node ();
	auto prepare = [&] () {
		lumex::state_block_builder builder;
		std::deque<std::shared_ptr<lumex::block>> blocks;
		std::deque<lumex::keypair> keys;
		auto previous = *std::prev (std::prev (system.initialization_blocks.end ()));
		for (auto i = 0; i < 20000; ++i)
		{
			keys.emplace_back ();
			auto const & key = keys.back ();
			auto block = builder.make_block ()
						 .account (lumex::dev::genesis_key.pub)
						 .representative (lumex::dev::genesis_key.pub)
						 .previous (previous->hash ())
						 .link (key.pub)
						 .balance (previous->balance_field ().value ().number () - 1000 * lumex::raw_ratio)
						 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
						 .work (*system.work.generate (previous->hash ()))
						 .build ();
			blocks.push_back (block);
			previous = block;
		}
		return std::make_tuple (blocks, keys);
	};
	auto const & [blocks, keys] = prepare ();
	auto execute = [&] () {
		auto count = 0;
		for (auto block : blocks)
		{
			ASSERT_EQ (lumex::block_status::progress, node.process (block));
		}
	};
	execute ();
}

TEST (flamegraph, large_confirmation)
{
	auto reps = rep_set (4);
	auto circulating = 10 * lumex::Klumex_ratio;
	lumex::test::system system;
	system.ledger_initialization_set (reps, circulating);
	auto prepare = [&] () {
		lumex::state_block_builder builder;
		std::deque<std::shared_ptr<lumex::block>> blocks;
		std::deque<lumex::keypair> keys;
		auto previous = *std::prev (std::prev (system.initialization_blocks.end ()));
		for (auto i = 0; i < 100; ++i)
		{
			keys.emplace_back ();
			auto const & key = keys.back ();
			auto block = builder.make_block ()
						 .account (lumex::dev::genesis_key.pub)
						 .representative (lumex::dev::genesis_key.pub)
						 .previous (previous->hash ())
						 .link (key.pub)
						 .balance (previous->balance_field ().value ().number () - 1000 * lumex::raw_ratio)
						 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
						 .work (*system.work.generate (previous->hash ()))
						 .build ();
			blocks.push_back (block);
			previous = block;
		}
		return std::make_tuple (blocks, keys);
	};
	auto const & [blocks, keys] = prepare ();
	system.initialization_blocks.insert (system.initialization_blocks.end (), blocks.begin (), blocks.end ());
	lumex::node_config config;
	lumex::node_flags flags;
	auto & node1 = *system.add_node (config, flags, lumex::transport::transport_type::tcp, reps[0]);
	auto & node2 = *system.add_node (config, flags, lumex::transport::transport_type::tcp, reps[1]);
	auto & node3 = *system.add_node (config, flags, lumex::transport::transport_type::tcp, reps[2]);
	auto & node4 = *system.add_node (config, flags, lumex::transport::transport_type::tcp, reps[3]);
	ASSERT_TIMELY (300s, std::all_of (system.nodes.begin (), system.nodes.end (), [&] (auto const & node) {
		return node->block_confirmed (system.initialization_blocks.back ()->hash ());
	}));
}
