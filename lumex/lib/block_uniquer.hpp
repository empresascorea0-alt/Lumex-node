#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/lib/uniquer.hpp>

namespace lumex
{
class block;
using block_uniquer = lumex::uniquer<lumex::uint256_union, lumex::block>;
}
