#include <lumex/crypto_lib/random_pool.hpp>
#include <lumex/lib/blockbuilders.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/files.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/timer.hpp>
#include <lumex/lib/work.hpp>
#include <lumex/lib/work_version.hpp>
#include <lumex/node/openclconfig.hpp>
#include <lumex/node/openclwork.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/network_params.hpp>

#include <gtest/gtest.h>

#include <future>

// produce one proof of work for a block and check that its difficulty is higher than the base difficulty
TEST (work, one)
{
	lumex::work_pool pool{ lumex::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	lumex::block_builder builder;
	auto block = builder
				 .change ()
				 .previous (1)
				 .representative (1)
				 .sign (lumex::keypair ().prv, 3)
				 .work (4)
				 .build ();
	block->block_work_set (*pool.generate (block->root ()));
	ASSERT_LT (lumex::dev::network_params.work.threshold_base (block->work_version ()), lumex::dev::network_params.work.difficulty (*block));
}

// create a work_pool with zero threads and check that pool.generate returns no result
TEST (work, disabled)
{
	lumex::work_pool pool{ lumex::dev::network_params.network, 0 };
	auto result (pool.generate (lumex::block_hash ()));
	ASSERT_FALSE (result.has_value ());
}

// create a block with bad pow then fix it and check that it validates
TEST (work, validate)
{
	lumex::work_pool pool{ lumex::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	lumex::block_builder builder;
	auto send_block = builder
					  .send ()
					  .previous (1)
					  .destination (1)
					  .balance (2)
					  .sign (lumex::keypair ().prv, 4)
					  .work (6)
					  .build ();
	ASSERT_LT (lumex::dev::network_params.work.difficulty (*send_block), lumex::dev::network_params.work.threshold_base (send_block->work_version ()));
	send_block->block_work_set (*pool.generate (send_block->root ()));
	ASSERT_GE (lumex::dev::network_params.work.difficulty (*send_block), lumex::dev::network_params.work.threshold_base (send_block->work_version ()));
}

// repeatedly start and cancel a work calculation and check that the callback is eventually called
TEST (work, cancel)
{
	lumex::work_pool pool{ lumex::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	const lumex::root key (1);
	auto iterations = 0;
	auto done = false;
	while (!done)
	{
		pool.generate (lumex::work_version::work_1, key, lumex::dev::network_params.work.base, [&done] (std::optional<uint64_t> work_a) {
			done = !work_a;
		});
		pool.cancel (key);
		++iterations;
		ASSERT_LT (iterations, 200);
	}
}

TEST (work, cancel_one_out_of_many)
{
	lumex::work_pool pool{ lumex::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	lumex::root key1 (1);
	lumex::root key2 (2);
	lumex::root key3 (1);
	lumex::root key4 (1);
	lumex::root key5 (3);
	lumex::root key6 (1);
	pool.generate (lumex::work_version::work_1, key1, lumex::dev::network_params.work.base, [] (std::optional<uint64_t>) {});
	pool.generate (lumex::work_version::work_1, key2, lumex::dev::network_params.work.base, [] (std::optional<uint64_t>) {});
	pool.generate (lumex::work_version::work_1, key3, lumex::dev::network_params.work.base, [] (std::optional<uint64_t>) {});
	pool.generate (lumex::work_version::work_1, key4, lumex::dev::network_params.work.base, [] (std::optional<uint64_t>) {});
	pool.generate (lumex::work_version::work_1, key5, lumex::dev::network_params.work.base, [] (std::optional<uint64_t>) {});
	pool.generate (lumex::work_version::work_1, key6, lumex::dev::network_params.work.base, [] (std::optional<uint64_t>) {});
	pool.cancel (key1);
}

// check that opencl hardware offloading works
TEST (work, opencl)
{
	lumex::logger logger;
	bool error = false;
	lumex::opencl_environment environment (error);
	ASSERT_TRUE (!error || !lumex::opencl_loaded);

	if (environment.platforms.empty () || environment.platforms.begin ()->devices.empty ())
	{
		GTEST_SKIP () << "Device with OpenCL support not found. Skipping OpenCL test" << std::endl;
	}

	lumex::opencl_config config (0, 0, 16 * 1024);
	auto opencl = lumex::opencl_work::create (true, config, logger, lumex::dev::network_params.work);
	ASSERT_TRUE (opencl);

	// 0 threads, should add 1 for managing OpenCL
	bool opencl_function_called = false;
	lumex::work_pool pool{ lumex::dev::network_params.network, 0, std::chrono::seconds (0),
		[&opencl, &opencl_function_called] (lumex::work_version const version_a, lumex::root const & root_a, uint64_t difficulty_a, std::atomic<int> & ticket_a) {
			opencl_function_called = true;
			return opencl->generate_work (version_a, root_a, difficulty_a);
		} };
	ASSERT_NE (nullptr, pool.opencl);

	lumex::root root;
	uint64_t difficulty (0xffff000000000000);
	uint64_t difficulty_add (0x00000f0000000000);
	for (auto i (0); i < 16; ++i)
	{
		lumex::random_pool::generate_block (root.bytes.data (), root.bytes.size ());
		auto nonce_opt = pool.generate (lumex::work_version::work_1, root, difficulty);
		ASSERT_TRUE (nonce_opt.has_value ());
		auto nonce = nonce_opt.value ();
		ASSERT_GE (lumex::dev::network_params.work.difficulty (lumex::work_version::work_1, root, nonce), difficulty);
		difficulty += difficulty_add;
	}
	ASSERT_TRUE (opencl_function_called);
}

// repeat difficulty calculations until a difficulty in a certain range is found
TEST (work, difficulty)
{
	lumex::work_pool pool{ lumex::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	const lumex::root root (1);
	uint64_t difficulty1 = 0xff00000000000000;
	uint64_t difficulty2 = 0xfff0000000000000;
	uint64_t difficulty3 = 0xffff000000000000;

	// find a difficulty between difficulty1 and difficulty2
	uint64_t result_difficulty1 = 0;
	do
	{
		auto work1 = *pool.generate (lumex::work_version::work_1, root, difficulty1);
		result_difficulty1 = lumex::dev::network_params.work.difficulty (lumex::work_version::work_1, root, work1);
	} while (result_difficulty1 > difficulty2);
	ASSERT_GT (result_difficulty1, difficulty1);

	// find a difficulty between difficulty2 and difficulty3
	uint64_t result_difficulty2 (0);
	do
	{
		auto work2 = *pool.generate (lumex::work_version::work_1, root, difficulty2);
		result_difficulty2 = lumex::dev::network_params.work.difficulty (lumex::work_version::work_1, root, work2);
	} while (result_difficulty2 > difficulty3);
	ASSERT_GT (result_difficulty2, difficulty2);
}

// check that the pow_rate_limiter of work_pool works, this test can fail occasionally
TEST (work, eco_pow)
{
	auto work_func = [] (std::promise<std::chrono::seconds> & promise, std::chrono::seconds interval) {
		lumex::work_pool pool{ lumex::dev::network_params.network, 1, interval };
		constexpr auto num_iterations = 5;

		lumex::timer<std::chrono::seconds> timer;
		timer.start ();
		for (int i = 0; i < num_iterations; ++i)
		{
			lumex::root root (1);
			uint64_t difficulty1 (0xff00000000000000);
			uint64_t difficulty2 (0xfff0000000000000);
			uint64_t result_difficulty (0);
			do
			{
				auto work = *pool.generate (lumex::work_version::work_1, root, difficulty1);
				result_difficulty = lumex::dev::network_params.work.difficulty (lumex::work_version::work_1, root, work);
			} while (result_difficulty > difficulty2);
			ASSERT_GT (result_difficulty, difficulty1);
		}

		promise.set_value_at_thread_exit (timer.stop ());
	};

	std::promise<std::chrono::seconds> promise1;
	std::future<std::chrono::seconds> future1 = promise1.get_future ();
	std::promise<std::chrono::seconds> promise2;
	std::future<std::chrono::seconds> future2 = promise2.get_future ();

	std::thread thread1 (work_func, std::ref (promise1), std::chrono::seconds (0));
	std::thread thread2 (work_func, std::ref (promise2), std::chrono::milliseconds (10));

	thread1.join ();
	thread2.join ();

	// Confirm that the eco pow rate limiter is working.
	// It's possible under some unlucky circumstances that this fails to the random nature of valid work generation.
	ASSERT_LT (future1.get (), future2.get ());
}
