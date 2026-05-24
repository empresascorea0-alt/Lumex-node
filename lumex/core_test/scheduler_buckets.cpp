#include <lumex/lib/blockbuilders.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/ratios.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/network_params.hpp>

#include <gtest/gtest.h>

#include <unordered_set>

lumex::keypair & keyzero ()
{
	static lumex::keypair result;
	return result;
}
lumex::keypair & key0 ()
{
	static lumex::keypair result;
	return result;
}
lumex::keypair & key1 ()
{
	static lumex::keypair result;
	return result;
}
lumex::keypair & key2 ()
{
	static lumex::keypair result;
	return result;
}
lumex::keypair & key3 ()
{
	static lumex::keypair result;
	return result;
}
std::shared_ptr<lumex::state_block> & blockzero ()
{
	lumex::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (keyzero ().pub)
						 .previous (0)
						 .representative (keyzero ().pub)
						 .balance (0)
						 .link (0)
						 .sign (keyzero ().prv, keyzero ().pub)
						 .work (0)
						 .build ();
	return result;
}
std::shared_ptr<lumex::state_block> & block0 ()
{
	lumex::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key0 ().pub)
						 .previous (0)
						 .representative (key0 ().pub)
						 .balance (lumex::Klumex_ratio)
						 .link (0)
						 .sign (key0 ().prv, key0 ().pub)
						 .work (0)
						 .build ();
	return result;
}
std::shared_ptr<lumex::state_block> & block1 ()
{
	lumex::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key1 ().pub)
						 .previous (0)
						 .representative (key1 ().pub)
						 .balance (lumex::lumex_ratio)
						 .link (0)
						 .sign (key1 ().prv, key1 ().pub)
						 .work (0)
						 .build ();
	return result;
}
std::shared_ptr<lumex::state_block> & block2 ()
{
	lumex::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key2 ().pub)
						 .previous (0)
						 .representative (key2 ().pub)
						 .balance (lumex::Klumex_ratio)
						 .link (0)
						 .sign (key2 ().prv, key2 ().pub)
						 .work (0)
						 .build ();
	return result;
}
std::shared_ptr<lumex::state_block> & block3 ()
{
	lumex::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key3 ().pub)
						 .previous (0)
						 .representative (key3 ().pub)
						 .balance (lumex::lumex_ratio)
						 .link (0)
						 .sign (key3 ().prv, key3 ().pub)
						 .work (0)
						 .build ();
	return result;
}

/*
TEST (buckets, construction)
{
	lumex::scheduler::buckets buckets;
	ASSERT_EQ (0, buckets.size ());
	ASSERT_TRUE (buckets.empty ());
	ASSERT_EQ (63, buckets.bucket_count ());
}

TEST (buckets, insert_Klumex)
{
	lumex::scheduler::buckets buckets;
	buckets.push (1000, block0 (), lumex::Klumex_ratio);
	ASSERT_EQ (1, buckets.size ());
	ASSERT_EQ (1, buckets.bucket_size (49));
}

TEST (buckets, insert_Mxrb)
{
	lumex::scheduler::buckets buckets;
	buckets.push (1000, block1 (), lumex::lumex_ratio);
	ASSERT_EQ (1, buckets.size ());
	ASSERT_EQ (1, buckets.bucket_size (14));
}

// Test two blocks with the same priority
TEST (buckets, insert_same_priority)
{
	lumex::scheduler::buckets buckets;
	buckets.push (1000, block0 (), lumex::Klumex_ratio);
	buckets.push (1000, block2 (), lumex::Klumex_ratio);
	ASSERT_EQ (2, buckets.size ());
	ASSERT_EQ (2, buckets.bucket_size (49));
}

// Test the same block inserted multiple times
TEST (buckets, insert_duplicate)
{
	lumex::scheduler::buckets buckets;
	buckets.push (1000, block0 (), lumex::Klumex_ratio);
	buckets.push (1000, block0 (), lumex::Klumex_ratio);
	ASSERT_EQ (1, buckets.size ());
	ASSERT_EQ (1, buckets.bucket_size (49));
}

TEST (buckets, insert_older)
{
	lumex::scheduler::buckets buckets;
	buckets.push (1000, block0 (), lumex::Klumex_ratio);
	buckets.push (1100, block2 (), lumex::Klumex_ratio);
	ASSERT_EQ (block0 (), buckets.top ());
	buckets.pop ();
	ASSERT_EQ (block2 (), buckets.top ());
	buckets.pop ();
}

TEST (buckets, pop)
{
	lumex::scheduler::buckets buckets;
	ASSERT_TRUE (buckets.empty ());
	buckets.push (1000, block0 (), lumex::Klumex_ratio);
	ASSERT_FALSE (buckets.empty ());
	buckets.pop ();
	ASSERT_TRUE (buckets.empty ());
}

TEST (buckets, top_one)
{
	lumex::scheduler::buckets buckets;
	buckets.push (1000, block0 (), lumex::Klumex_ratio);
	ASSERT_EQ (block0 (), buckets.top ());
}

TEST (buckets, top_two)
{
	lumex::scheduler::buckets buckets;
	buckets.push (1000, block0 (), lumex::Klumex_ratio);
	buckets.push (1, block1 (), lumex::lumex_ratio);
	ASSERT_EQ (block0 (), buckets.top ());
	buckets.pop ();
	ASSERT_EQ (block1 (), buckets.top ());
	buckets.pop ();
	ASSERT_TRUE (buckets.empty ());
}

TEST (buckets, top_round_robin)
{
	lumex::scheduler::buckets buckets;
	buckets.push (1000, blockzero (), 0);
	ASSERT_EQ (blockzero (), buckets.top ());
	buckets.push (1000, block0 (), lumex::Klumex_ratio);
	buckets.push (1000, block1 (), lumex::lumex_ratio);
	buckets.push (1100, block3 (), lumex::lumex_ratio);
	buckets.pop (); // blockzero
	EXPECT_EQ (block1 (), buckets.top ());
	buckets.pop ();
	EXPECT_EQ (block0 (), buckets.top ());
	buckets.pop ();
	EXPECT_EQ (block3 (), buckets.top ());
	buckets.pop ();
	EXPECT_TRUE (buckets.empty ());
}

TEST (buckets, trim_normal)
{
	lumex::scheduler::buckets buckets{ 1 };
	buckets.push (1000, block0 (), lumex::Klumex_ratio);
	buckets.push (1100, block2 (), lumex::Klumex_ratio);
	ASSERT_EQ (1, buckets.size ());
	ASSERT_EQ (block0 (), buckets.top ());
}

TEST (buckets, trim_reverse)
{
	lumex::scheduler::buckets buckets{ 1 };
	buckets.push (1100, block2 (), lumex::Klumex_ratio);
	buckets.push (1000, block0 (), lumex::Klumex_ratio);
	ASSERT_EQ (1, buckets.size ());
	ASSERT_EQ (block0 (), buckets.top ());
}

TEST (buckets, trim_even)
{
	lumex::scheduler::buckets buckets{ 2 };
	buckets.push (1000, block0 (), lumex::Klumex_ratio);
	buckets.push (1100, block2 (), lumex::Klumex_ratio);
	ASSERT_EQ (1, buckets.size ());
	ASSERT_EQ (block0 (), buckets.top ());
	buckets.push (1000, block1 (), lumex::lumex_ratio);
	ASSERT_EQ (2, buckets.size ());
	ASSERT_EQ (block0 (), buckets.top ());
	buckets.pop ();
	ASSERT_EQ (block1 (), buckets.top ());
}
*/