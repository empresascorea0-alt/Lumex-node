#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/node/fwd.hpp>

#include <boost/multiprecision/cpp_int.hpp>

#include <vector>

namespace lumex
{
class bucketing
{
public:
	bucketing ();

	lumex::bucket_index bucket_index (lumex::amount balance) const;
	std::vector<lumex::bucket_index> const & bucket_indices () const;
	size_t size () const;

private:
	std::vector<lumex::uint128_t> minimums;
	std::vector<lumex::bucket_index> indices;
};
}