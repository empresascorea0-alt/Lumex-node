#include <lumex/lib/blockbuilders.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/backlog_scan.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/test_common/chains.hpp>
#include <lumex/test_common/system.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <numeric>
#include <unordered_set>

using namespace std::chrono_literals;

/*
 * Ensures all not confirmed accounts get activated by backlog scan periodically
 */
TEST (backlog, population)
{
	lumex::mutex mutex;
	std::unordered_set<lumex::account> activated;

	lumex::test::system system{};
	auto & node = *system.add_node ();

	node.backlog_scan.batch_activated.add ([&] (auto const & batch) {
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		for (auto const & info : batch)
		{
			activated.insert (info.account);
		}
	});

	auto blocks = lumex::test::setup_independent_blocks (system, node, 256);

	// Checks if `activated` set contains all accounts we previously set up
	auto all_activated = [&] () {
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		return std::all_of (blocks.begin (), blocks.end (), [&] (auto const & item) {
			return activated.count (item->account ()) != 0;
		});
	};
	ASSERT_TIMELY (5s, all_activated ());

	// Clear activated set to ensure we activate those accounts more than once
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		activated.clear ();
	}

	ASSERT_TIMELY (5s, all_activated ());
}

/*
 * Ensures that elections are activated without live traffic
 */
TEST (backlog, election_activation)
{
	lumex::test::system system;
	lumex::node_config node_config = system.default_config ();
	auto & node = *system.add_node (node_config);
	lumex::keypair key;
	lumex::block_builder builder;
	auto send = builder
				.state ()
				.account (lumex::dev::genesis_key.pub)
				.previous (lumex::dev::genesis->hash ())
				.representative (lumex::dev::genesis_key.pub)
				.balance (lumex::dev::constants.genesis_amount - lumex::Klumex_ratio)
				.link (key.pub)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*node.work_generate_blocking (lumex::dev::genesis->hash ()))
				.build ();
	{
		auto transaction = node.ledger.tx_begin_write ();
		ASSERT_EQ (lumex::block_status::progress, node.ledger.process (transaction, send));
	}
	ASSERT_TIMELY_EQ (5s, node.active.size (), 1);
}