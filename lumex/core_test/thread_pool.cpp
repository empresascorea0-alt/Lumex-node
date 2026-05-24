#include <lumex/lib/thread_pool.hpp>
#include <lumex/lib/timer.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <future>

TEST (thread_pool, thread_pool)
{
	std::atomic<bool> passed_sleep{ false };

	auto func = [&passed_sleep] () {
		std::this_thread::sleep_for (std::chrono::seconds (1));
		passed_sleep = true;
	};

	lumex::thread_pool workers (1u, lumex::thread_role::name::unknown);
	lumex::test::start_stop_guard stop_guard{ workers };
	workers.post (func);
	ASSERT_FALSE (passed_sleep);

	lumex::timer<std::chrono::milliseconds> timer_l;
	timer_l.start ();
	while (!passed_sleep)
	{
		if (timer_l.since_start () > std::chrono::seconds (10))
		{
			break;
		}
	}
	ASSERT_TRUE (passed_sleep);
}

TEST (thread_pool, one)
{
	std::atomic<bool> done (false);
	lumex::mutex mutex;
	lumex::condition_variable condition;
	lumex::thread_pool workers (1u, lumex::thread_role::name::unknown);
	lumex::test::start_stop_guard stop_guard{ workers };
	workers.post ([&] () {
		{
			lumex::lock_guard<lumex::mutex> lock{ mutex };
			done = true;
		}
		condition.notify_one ();
	});
	lumex::unique_lock<lumex::mutex> unique{ mutex };
	condition.wait (unique, [&] () { return !!done; });
}

TEST (thread_pool, many)
{
	std::atomic<int> count (0);
	lumex::mutex mutex;
	lumex::condition_variable condition;
	lumex::thread_pool workers (50u, lumex::thread_role::name::unknown);
	lumex::test::start_stop_guard stop_guard{ workers };
	for (auto i (0); i < 50; ++i)
	{
		workers.post ([&] () {
			{
				lumex::lock_guard<lumex::mutex> lock{ mutex };
				count += 1;
			}
			condition.notify_one ();
		});
	}
	lumex::unique_lock<lumex::mutex> unique{ mutex };
	condition.wait (unique, [&] () { return count == 50; });
}

TEST (thread_pool, top_execution)
{
	int value1 (0);
	int value2 (0);
	lumex::mutex mutex;
	std::promise<bool> promise;
	lumex::thread_pool workers (1u, lumex::thread_role::name::unknown);
	lumex::test::start_stop_guard stop_guard{ workers };
	workers.post ([&] () {
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		value1 = 1;
		value2 = 1;
	});
	workers.post_delayed (std::chrono::milliseconds (1), [&] () {
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		value2 = 2;
		promise.set_value (false);
	});
	promise.get_future ().get ();
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	ASSERT_EQ (1, value1);
	ASSERT_EQ (2, value2);
}