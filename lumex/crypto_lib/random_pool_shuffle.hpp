#pragma once

#include <lumex/crypto_lib/random_pool.hpp>

#include <cryptopp/osrng.h>

namespace lumex
{
template <class Iter>
void random_pool_shuffle (Iter begin, Iter end)
{
	random_pool::get_pool ().Shuffle (begin, end);
}
}
