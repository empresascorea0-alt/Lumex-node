#pragma once

#include <limits>

namespace lumex
{
// Saturating arithmetic — clamps at numeric limits instead of overflowing
template <typename T>
T add_sat (T const & value, T const & diff) noexcept
{
	static_assert (std::numeric_limits<T>::is_specialized, "std::numeric_limits<T> must be specialized");
	return (value > std::numeric_limits<T>::max () - diff) ? std::numeric_limits<T>::max () : value + diff;
}
template <typename T>
T sub_sat (T const & value, T const & diff) noexcept
{
	static_assert (std::numeric_limits<T>::is_specialized, "std::numeric_limits<T> must be specialized");
	return (value < std::numeric_limits<T>::min () + diff) ? std::numeric_limits<T>::min () : value - diff;
}
template <typename T>
T inc_sat (T const & value) noexcept
{
	return add_sat (value, static_cast<T> (1));
}
template <typename T>
T dec_sat (T const & value) noexcept
{
	return sub_sat (value, static_cast<T> (1));
}
}
