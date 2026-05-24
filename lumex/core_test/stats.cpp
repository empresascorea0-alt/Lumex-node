#include <lumex/test_common/system.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <ostream>

// Test stat counting at both type and detail levels
TEST (stats, counters)
{
	lumex::test::system system;
	auto & node = *system.add_node ();

	// Initial state
	ASSERT_EQ (0, node.stats.count (lumex::stat::type::ledger, lumex::stat::dir::in));
	ASSERT_EQ (0, node.stats.count (lumex::stat::type::ledger, lumex::stat::detail::test, lumex::stat::dir::in));
	ASSERT_EQ (0, node.stats.count (lumex::stat::type::ledger, lumex::stat::detail::send, lumex::stat::dir::out));

	node.stats.add (lumex::stat::type::ledger, lumex::stat::detail::test, lumex::stat::dir::in, 1);
	node.stats.add (lumex::stat::type::ledger, lumex::stat::detail::test, lumex::stat::dir::in, 5);
	node.stats.inc (lumex::stat::type::ledger, lumex::stat::detail::test, lumex::stat::dir::in);
	node.stats.inc (lumex::stat::type::ledger, lumex::stat::detail::send, lumex::stat::dir::in);
	node.stats.inc (lumex::stat::type::ledger, lumex::stat::detail::send, lumex::stat::dir::in);
	node.stats.inc (lumex::stat::type::ledger, lumex::stat::detail::receive, lumex::stat::dir::in);

	ASSERT_EQ (10, node.stats.count (lumex::stat::type::ledger, lumex::stat::dir::in));
	ASSERT_EQ (2, node.stats.count (lumex::stat::type::ledger, lumex::stat::detail::send, lumex::stat::dir::in));
	ASSERT_EQ (1, node.stats.count (lumex::stat::type::ledger, lumex::stat::detail::receive, lumex::stat::dir::in));

	node.stats.add (lumex::stat::type::ledger, lumex::stat::detail::test, lumex::stat::dir::in, 0);

	ASSERT_EQ (10, node.stats.count (lumex::stat::type::ledger, lumex::stat::dir::in));
}

TEST (stats, counters_aggregate_all)
{
	lumex::test::system system;
	auto & node = *system.add_node ();

	node.stats.add (lumex::stat::type::ledger, lumex::stat::detail::test, lumex::stat::dir::in, 1, true);

	ASSERT_EQ (1, node.stats.count (lumex::stat::type::ledger, lumex::stat::dir::in));
	ASSERT_EQ (1, node.stats.count (lumex::stat::type::ledger, lumex::stat::detail::all, lumex::stat::dir::in));
	ASSERT_EQ (1, node.stats.count (lumex::stat::type::ledger, lumex::stat::detail::test, lumex::stat::dir::in));

	node.stats.add (lumex::stat::type::ledger, lumex::stat::detail::activate, lumex::stat::dir::in, 5, true);

	ASSERT_EQ (6, node.stats.count (lumex::stat::type::ledger, lumex::stat::dir::in));
	ASSERT_EQ (6, node.stats.count (lumex::stat::type::ledger, lumex::stat::detail::all, lumex::stat::dir::in));
	ASSERT_EQ (1, node.stats.count (lumex::stat::type::ledger, lumex::stat::detail::test, lumex::stat::dir::in));
}

TEST (stats, samples)
{
	lumex::test::system system;
	auto & node = *system.add_node ();

	node.stats.sample (lumex::stat::sample::active_election_duration, 5, { 1, 10 });
	node.stats.sample (lumex::stat::sample::active_election_duration, 5, { 1, 10 });
	node.stats.sample (lumex::stat::sample::active_election_duration, 11, { 1, 10 });
	node.stats.sample (lumex::stat::sample::active_election_duration, 37, { 1, 10 });

	node.stats.sample (lumex::stat::sample::bootstrap_tag_duration, 2137, { 1, 10 });

	auto samples1 = node.stats.samples (lumex::stat::sample::active_election_duration);
	ASSERT_EQ (4, samples1.size ());
	ASSERT_EQ (5, samples1[0]);
	ASSERT_EQ (5, samples1[1]);
	ASSERT_EQ (11, samples1[2]);
	ASSERT_EQ (37, samples1[3]);

	auto samples2 = node.stats.samples (lumex::stat::sample::active_election_duration);
	ASSERT_EQ (0, samples2.size ());

	node.stats.sample (lumex::stat::sample::active_election_duration, 3, { 1, 10 });

	auto samples3 = node.stats.samples (lumex::stat::sample::active_election_duration);
	ASSERT_EQ (1, samples3.size ());
	ASSERT_EQ (3, samples3[0]);

	auto samples4 = node.stats.samples (lumex::stat::sample::bootstrap_tag_duration);
	ASSERT_EQ (1, samples4.size ());
	ASSERT_EQ (2137, samples4[0]);
}