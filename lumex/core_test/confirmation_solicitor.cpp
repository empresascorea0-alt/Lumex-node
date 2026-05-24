#include <lumex/lib/blockbuilders.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/jsonconfig.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/block_rebroadcaster.hpp>
#include <lumex/node/confirmation_solicitor.hpp>
#include <lumex/node/election.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/transport/inproc.hpp>
#include <lumex/test_common/network.hpp>
#include <lumex/test_common/system.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (confirmation_solicitor, batches)
{
	lumex::test::system system;
	lumex::node_flags node_flags;
	node_flags.disable_request_loop = true;
	node_flags.disable_rep_crawler = true;
	lumex::node_config config1 = system.default_config ();
	config1.block_rebroadcaster->enable = false;
	auto & node1 = *system.add_node (config1, node_flags);
	lumex::node_config config2 = system.default_config ();
	config2.block_rebroadcaster->enable = false;
	auto & node2 = *system.add_node (config2, node_flags);
	auto channel1 = lumex::test::establish_tcp (system, node2, node1.network.endpoint ());
	// Solicitor will only solicit from this representative
	lumex::representative representative{ lumex::dev::genesis_key.pub, channel1 };
	std::vector<lumex::representative> representatives{ representative };
	lumex::confirmation_solicitor solicitor (node2.network, node2.config);
	solicitor.prepare (representatives);
	// Ensure the representatives are correct
	ASSERT_EQ (1, representatives.size ());
	ASSERT_EQ (channel1, representatives.front ().channel);
	ASSERT_EQ (lumex::dev::genesis_key.pub, representatives.front ().account);
	ASSERT_TIMELY_EQ (3s, node2.network.size (), 1);
	lumex::block_builder builder;
	auto send = builder
				.send ()
				.previous (lumex::dev::genesis->hash ())
				.destination (lumex::keypair ().pub)
				.balance (lumex::dev::constants.genesis_amount - 100)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*system.work.generate (lumex::dev::genesis->hash ()))
				.build ();
	send->sideband_set ({});
	for (size_t i (0); i < lumex::network::confirm_req_hashes_max; ++i)
	{
		auto election (std::make_shared<lumex::election> (node2, send, lumex::election_behavior::priority));
		ASSERT_FALSE (solicitor.add (*election));
	}
	// Reached the maximum amount of requests for the channel
	auto election (std::make_shared<lumex::election> (node2, send, lumex::election_behavior::priority));
	// Broadcasting should be immediate
	ASSERT_EQ (0, node2.stats.count (lumex::stat::type::message, lumex::stat::detail::publish, lumex::stat::dir::out));
	ASSERT_FALSE (solicitor.broadcast (*election));
	// One publish through directed broadcasting (random flooding moved to block_rebroadcaster)
	ASSERT_EQ (1, node2.stats.count (lumex::stat::type::message, lumex::stat::detail::publish, lumex::stat::dir::out));
	solicitor.flush ();
	ASSERT_EQ (1, node2.stats.count (lumex::stat::type::message, lumex::stat::detail::confirm_req, lumex::stat::dir::out));
}

namespace lumex
{
TEST (confirmation_solicitor, different_hash)
{
	lumex::test::system system;
	lumex::node_flags node_flags;
	node_flags.disable_request_loop = true;
	node_flags.disable_rep_crawler = true;
	lumex::node_config config1 = system.default_config ();
	config1.block_rebroadcaster->enable = false;
	auto & node1 = *system.add_node (config1, node_flags);
	lumex::node_config config2 = system.default_config ();
	config2.block_rebroadcaster->enable = false;
	auto & node2 = *system.add_node (config2, node_flags);
	auto channel1 = lumex::test::establish_tcp (system, node2, node1.network.endpoint ());
	// Solicitor will only solicit from this representative
	lumex::representative representative{ lumex::dev::genesis_key.pub, channel1 };
	std::vector<lumex::representative> representatives{ representative };
	lumex::confirmation_solicitor solicitor (node2.network, node2.config);
	solicitor.prepare (representatives);
	// Ensure the representatives are correct
	ASSERT_EQ (1, representatives.size ());
	ASSERT_EQ (channel1, representatives.front ().channel);
	ASSERT_EQ (lumex::dev::genesis_key.pub, representatives.front ().account);
	ASSERT_TIMELY_EQ (3s, node2.network.size (), 1);
	lumex::block_builder builder;
	auto send = builder
				.send ()
				.previous (lumex::dev::genesis->hash ())
				.destination (lumex::keypair ().pub)
				.balance (lumex::dev::constants.genesis_amount - 100)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*system.work.generate (lumex::dev::genesis->hash ()))
				.build ();
	send->sideband_set ({});
	auto election (std::make_shared<lumex::election> (node2, send, lumex::election_behavior::priority));
	// Add a vote for something else, not the winner
	election->last_votes[representative.account] = { std::chrono::steady_clock::now (), 1, 1 };
	// Ensure the request and broadcast goes through
	ASSERT_FALSE (solicitor.add (*election));
	ASSERT_FALSE (solicitor.broadcast (*election));
	// One publish through directed broadcasting (random flooding moved to block_rebroadcaster)
	ASSERT_EQ (1, node2.stats.count (lumex::stat::type::message, lumex::stat::detail::publish, lumex::stat::dir::out));
	solicitor.flush ();
	ASSERT_EQ (1, node2.stats.count (lumex::stat::type::message, lumex::stat::detail::confirm_req, lumex::stat::dir::out));
}

TEST (confirmation_solicitor, bypass_max_requests_cap)
{
	lumex::test::system system;
	lumex::node_flags node_flags;
	node_flags.disable_request_loop = true;
	node_flags.disable_rep_crawler = true;
	auto & node1 = *system.add_node (node_flags);
	auto & node2 = *system.add_node (node_flags);
	lumex::confirmation_solicitor solicitor (node2.network, node2.config);
	std::vector<lumex::representative> representatives;
	auto max_representatives = std::max<size_t> (solicitor.max_election_requests, solicitor.max_election_broadcasts);
	representatives.reserve (max_representatives + 1);
	for (auto i (0); i < max_representatives + 1; ++i)
	{
		// Make a temporary channel associated with node2
		auto channel = std::make_shared<lumex::transport::inproc::channel> (node2, node2);
		lumex::representative representative{ lumex::account (i), channel };
		representatives.push_back (representative);
	}
	ASSERT_EQ (max_representatives + 1, representatives.size ());
	solicitor.prepare (representatives);
	ASSERT_TIMELY_EQ (3s, node2.network.size (), 1);
	lumex::block_builder builder;
	auto send = builder
				.send ()
				.previous (lumex::dev::genesis->hash ())
				.destination (lumex::keypair ().pub)
				.balance (lumex::dev::constants.genesis_amount - 100)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*system.work.generate (lumex::dev::genesis->hash ()))
				.build ();
	send->sideband_set ({});
	auto election (std::make_shared<lumex::election> (node2, send, lumex::election_behavior::priority));
	// Add a vote for something else, not the winner
	for (auto const & rep : representatives)
	{
		election->set_last_vote (rep.account, { std::chrono::steady_clock::now (), 1, 1 });
	}
	ASSERT_FALSE (solicitor.add (*election));
	ASSERT_FALSE (solicitor.broadcast (*election));
	solicitor.flush ();
	// All requests went through, the last one would normally not go through due to the cap but a vote for a different hash does not count towards the cap
	ASSERT_TIMELY_EQ (6s, max_representatives + 1, node2.stats.count (lumex::stat::type::message, lumex::stat::detail::confirm_req, lumex::stat::dir::out));

	solicitor.prepare (representatives);
	auto election2 (std::make_shared<lumex::election> (node2, send, lumex::election_behavior::priority));
	ASSERT_FALSE (solicitor.add (*election2));
	ASSERT_FALSE (solicitor.broadcast (*election2));

	solicitor.flush ();

	// All requests but one went through, due to the cap
	ASSERT_EQ (2 * max_representatives + 1, node2.stats.count (lumex::stat::type::message, lumex::stat::detail::confirm_req, lumex::stat::dir::out));
}
}
