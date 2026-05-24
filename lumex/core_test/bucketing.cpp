#include <lumex/lib/ratios.hpp>
#include <lumex/node/bucketing.hpp>

#include <gtest/gtest.h>

#include <algorithm>

TEST (bucketing, construction)
{
	lumex::bucketing bucketing;
	ASSERT_EQ (63, bucketing.size ());
}

TEST (bucketing, zero_index)
{
	lumex::bucketing bucketing;
	ASSERT_EQ (0, bucketing.bucket_index (0));
}

TEST (bucketing, raw_index)
{
	lumex::bucketing bucketing;
	ASSERT_EQ (0, bucketing.bucket_index (lumex::raw_ratio));
}

TEST (bucketing, lumex_index)
{
	lumex::bucketing bucketing;
	ASSERT_EQ (14, bucketing.bucket_index (lumex::lumex_ratio));
}

TEST (bucketing, Klumex_index)
{
	lumex::bucketing bucketing;
	ASSERT_EQ (49, bucketing.bucket_index (lumex::Klumex_ratio));
}

TEST (bucketing, max_index)
{
	lumex::bucketing bucketing;
	ASSERT_EQ (62, bucketing.bucket_index (std::numeric_limits<lumex::amount::underlying_type>::max ()));
}

TEST (bucketing, indices)
{
	lumex::bucketing bucketing;
	auto indices = bucketing.bucket_indices ();
	ASSERT_EQ (63, indices.size ());
	ASSERT_EQ (indices.size (), bucketing.size ());

	// Check that the indices are in ascending order
	ASSERT_TRUE (std::adjacent_find (indices.begin (), indices.end (), [] (auto const & lhs, auto const & rhs) {
		return lhs >= rhs;
	})
	== indices.end ());
}