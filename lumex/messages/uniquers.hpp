#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/lib/uniquer.hpp>
#include <lumex/lib/vote.hpp>

namespace lumex
{
using block_uniquer = lumex::uniquer<lumex::uint256_union, lumex::block>;
using vote_uniquer = lumex::uniquer<lumex::block_hash, lumex::vote>;
}