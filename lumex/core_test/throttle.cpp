#include <lumex/node/bootstrap/throttle.hpp>

#include <gtest/gtest.h>

TEST (throttle, construction)
{
	lumex::bootstrap::throttle throttle{ 2 };
	ASSERT_FALSE (throttle.throttled ());
}

TEST (throttle, throttled)
{
	lumex::bootstrap::throttle throttle{ 2 };
	throttle.add (false);
	ASSERT_FALSE (throttle.throttled ());
	throttle.add (false);
	ASSERT_TRUE (throttle.throttled ());
}

TEST (throttle, resize_up)
{
	lumex::bootstrap::throttle throttle{ 2 };
	throttle.add (false);
	throttle.resize (4);
	ASSERT_FALSE (throttle.throttled ());
	throttle.add (false);
	ASSERT_TRUE (throttle.throttled ());
}

TEST (throttle, resize_down)
{
	lumex::bootstrap::throttle throttle{ 4 };
	throttle.add (false);
	ASSERT_FALSE (throttle.throttled ());
	throttle.resize (2);
	ASSERT_FALSE (throttle.throttled ());
	throttle.add (false);
	ASSERT_TRUE (throttle.throttled ());
}
