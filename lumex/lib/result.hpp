#pragma once

#include <lumex/boost/outcome.hpp>
#include <lumex/lib/errors.hpp>

namespace lumex
{
template <typename T>
using result = outcome::result<T, lumex::error>;
}