#pragma once

#include <chrono>

namespace nano::log
{
/**
 * Compute `numerator / denominator * 100` as a double (percentage).
 * Returns 100.0 when denominator is zero (semantic "all done").
 */
template <typename Num, typename Den>
inline double percentage (Num numerator, Den denominator)
{
	return denominator > 0 ? (static_cast<double> (numerator) / denominator * 100.0) : 100.0;
}
}

/**
 * Convert chrono durations to plain integer counts for log formatting.
 * Avoids verbose `duration_cast<...>(x).count()` boilerplate at every call site.
 */
namespace nano::log
{
template <class Clock>
auto microseconds (std::chrono::time_point<Clock> time)
{
	return std::chrono::duration_cast<std::chrono::microseconds> (time.time_since_epoch ()).count ();
}

template <class Duration>
auto microseconds (Duration duration)
{
	return std::chrono::duration_cast<std::chrono::microseconds> (duration).count ();
}

template <class Clock>
auto milliseconds (std::chrono::time_point<Clock> time)
{
	return std::chrono::duration_cast<std::chrono::milliseconds> (time.time_since_epoch ()).count ();
}

template <class Duration>
auto milliseconds (Duration duration)
{
	return std::chrono::duration_cast<std::chrono::milliseconds> (duration).count ();
}

template <class Clock>
auto seconds (std::chrono::time_point<Clock> time)
{
	return std::chrono::duration_cast<std::chrono::seconds> (time.time_since_epoch ()).count ();
}

template <class Duration>
auto seconds (Duration duration)
{
	return std::chrono::duration_cast<std::chrono::seconds> (duration).count ();
}

template <class Clock>
auto milliseconds_delta (std::chrono::time_point<Clock> time, std::chrono::time_point<Clock> now = Clock::now ())
{
	return std::chrono::duration_cast<std::chrono::milliseconds> (now - time).count ();
}

template <class Clock>
auto seconds_delta (std::chrono::time_point<Clock> time, std::chrono::time_point<Clock> now = Clock::now ())
{
	return std::chrono::duration_cast<std::chrono::seconds> (now - time).count ();
}
}
