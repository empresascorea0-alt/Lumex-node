#include <lumex/lib/blocks.hpp>
#include <lumex/lib/vote.hpp>
#include <lumex/node/endpoint.hpp>
#include <lumex/node/local_vote_history.hpp>
#include <lumex/node/vote_generator.hpp>
#include <lumex/node/vote_spacing.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/test_common/system.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace lumex
{
TEST (local_vote_history, basic)
{
	lumex::local_vote_history history{ lumex::dev::network_params.voting };
	ASSERT_FALSE (history.exists (1));
	ASSERT_FALSE (history.exists (2));
	ASSERT_TRUE (history.votes (1).empty ());
	ASSERT_TRUE (history.votes (2).empty ());
	auto vote1a (std::make_shared<lumex::vote> ());
	ASSERT_EQ (0, history.size ());
	history.add (1, 2, vote1a);
	ASSERT_EQ (1, history.size ());
	ASSERT_TRUE (history.exists (1));
	ASSERT_FALSE (history.exists (2));
	auto votes1a (history.votes (1));
	ASSERT_FALSE (votes1a.empty ());
	ASSERT_EQ (1, history.votes (1, 2).size ());
	ASSERT_TRUE (history.votes (1, 1).empty ());
	ASSERT_TRUE (history.votes (1, 3).empty ());
	ASSERT_TRUE (history.votes (2).empty ());
	ASSERT_EQ (1, votes1a.size ());
	ASSERT_EQ (vote1a, votes1a[0]);
	auto vote1b (std::make_shared<lumex::vote> ());
	history.add (1, 2, vote1b);
	auto votes1b (history.votes (1));
	ASSERT_EQ (1, votes1b.size ());
	ASSERT_EQ (vote1b, votes1b[0]);
	ASSERT_NE (vote1a, votes1b[0]);
	auto vote2 (std::make_shared<lumex::vote> ());
	vote2->account.dwords[0]++;
	ASSERT_EQ (1, history.size ());
	history.add (1, 2, vote2);
	ASSERT_EQ (2, history.size ());
	auto votes2 (history.votes (1));
	ASSERT_EQ (2, votes2.size ());
	ASSERT_TRUE (vote1b == votes2[0] || vote1b == votes2[1]);
	ASSERT_TRUE (vote2 == votes2[0] || vote2 == votes2[1]);
	auto vote3 (std::make_shared<lumex::vote> ());
	vote3->account.dwords[1]++;
	history.add (1, 3, vote3);
	ASSERT_EQ (1, history.size ());
	auto votes3 (history.votes (1));
	ASSERT_EQ (1, votes3.size ());
	ASSERT_EQ (vote3, votes3[0]);
}
}

// vote_generator tests moved to lumex/core_test/vote_generator.cpp

TEST (vote_spacing, basic)
{
	lumex::vote_spacing spacing{ std::chrono::milliseconds{ 100 } };
	lumex::root root1{ 1 };
	lumex::root root2{ 2 };
	lumex::block_hash hash3{ 3 };
	lumex::block_hash hash4{ 4 };
	lumex::block_hash hash5{ 5 };
	ASSERT_EQ (0, spacing.size ());
	ASSERT_TRUE (spacing.votable (root1, hash3));
	spacing.flag (root1, hash3);
	ASSERT_EQ (1, spacing.size ());
	ASSERT_TRUE (spacing.votable (root1, hash3));
	ASSERT_FALSE (spacing.votable (root1, hash4));
	spacing.flag (root2, hash5);
	ASSERT_EQ (2, spacing.size ());
}

TEST (vote_spacing, prune)
{
	auto length = std::chrono::milliseconds{ 100 };
	lumex::vote_spacing spacing{ length };
	lumex::root root1{ 1 };
	lumex::root root2{ 2 };
	lumex::block_hash hash3{ 3 };
	lumex::block_hash hash4{ 4 };
	spacing.flag (root1, hash3);
	ASSERT_EQ (1, spacing.size ());
	std::this_thread::sleep_for (length);
	spacing.flag (root2, hash4);
	ASSERT_EQ (1, spacing.size ());
}

// vote_spacing integration tests removed — vote_spacing is no longer part of vote_generator
