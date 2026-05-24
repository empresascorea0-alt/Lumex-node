#include <lumex/crypto/blake2/blake2.h>
#include <lumex/crypto_lib/random_pool.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/constants.hpp>
#include <lumex/lib/epoch.hpp>
#include <lumex/lib/thread_roles.hpp>
#include <lumex/lib/threading.hpp>
#include <lumex/lib/work.hpp>
#include <lumex/lib/work_version.hpp>
#include <lumex/node/xorshift.hpp>

#include <future>

std::string lumex::to_string (lumex::work_version const version_a)
{
	std::string result ("invalid");
	switch (version_a)
	{
		case lumex::work_version::work_1:
			result = "work_1";
			break;
		case lumex::work_version::unspecified:
			result = "unspecified";
			break;
	}
	return result;
}

lumex::work_pool::work_pool (lumex::network_constants & network_constants, unsigned max_threads_a, std::chrono::lumexseconds pow_rate_limiter_a, lumex::opencl_work_func_t opencl_a) :
	network_constants{ network_constants },
	ticket (0),
	done (false),
	pow_rate_limiter (pow_rate_limiter_a),
	opencl (opencl_a)
{
	static_assert (ATOMIC_INT_LOCK_FREE == 2, "Atomic int needed");

	auto count (network_constants.is_dev_network () ? std::min (max_threads_a, 1u) : std::min (max_threads_a, std::max (1u, lumex::hardware_concurrency ())));
	if (opencl)
	{
		// One thread to handle OpenCL
		++count;
	}
	for (auto i (0u); i < count; ++i)
	{
		threads.emplace_back ([this, i] () {
			lumex::thread_role::set (lumex::thread_role::name::work);
			lumex::work_thread_reprioritize ();
			loop (i);
		});
	}
}

lumex::work_pool::~work_pool ()
{
	stop ();
	for (auto & i : threads)
	{
		i.join ();
	}
}

void lumex::work_pool::loop (uint64_t thread)
{
	// Quick RNG for work attempts.
	xorshift1024star rng;
	lumex::random_pool::generate_block (reinterpret_cast<uint8_t *> (rng.s.data ()), rng.s.size () * sizeof (decltype (rng.s)::value_type));
	uint64_t work;
	uint64_t output;
	blake2b_state hash;
	blake2b_init (&hash, sizeof (output));
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	auto pow_sleep = pow_rate_limiter;
	while (!done)
	{
		auto empty (pending.empty ());
		if (thread == 0)
		{
			// Only work thread 0 notifies work observers
			work_observers.notify (!empty);
		}
		if (!empty)
		{
			auto current_l (pending.front ());
			int ticket_l (ticket);
			lock.unlock ();
			output = 0;
			std::optional<uint64_t> opt_work;
			if (thread == 0 && opencl)
			{
				opt_work = opencl (current_l.version, current_l.item, current_l.difficulty, ticket);
			}
			if (opt_work.has_value ())
			{
				work = *opt_work;
				output = network_constants.work.value (current_l.item, work);
			}
			else
			{
				// ticket != ticket_l indicates a different thread found a solution and we should stop
				while (ticket == ticket_l && output < current_l.difficulty)
				{
					// Don't query main memory every iteration in order to reduce memory bus traffic
					// All operations here operate on stack memory
					// Count iterations down to zero since comparing to zero is easier than comparing to another number
					unsigned iteration (256);
					while (iteration && output < current_l.difficulty)
					{
						work = rng.next ();
						blake2b_update (&hash, reinterpret_cast<uint8_t *> (&work), sizeof (work));
						blake2b_update (&hash, current_l.item.bytes.data (), current_l.item.bytes.size ());
						blake2b_final (&hash, reinterpret_cast<uint8_t *> (&output), sizeof (output));
						blake2b_init (&hash, sizeof (output));
						iteration -= 1;
					}

					// Add a rate limiter (if specified) to the pow calculation to save some CPUs which don't want to operate at full throttle
					if (pow_sleep != std::chrono::lumexseconds (0))
					{
						std::this_thread::sleep_for (pow_sleep);
					}
				}
			}
			lock.lock ();
			if (ticket == ticket_l)
			{
				// If the ticket matches what we started with, we're the ones that found the solution
				debug_assert (output >= current_l.difficulty);
				debug_assert (current_l.difficulty == 0 || network_constants.work.value (current_l.item, work) == output);
				// Signal other threads to stop their work next time they check ticket
				++ticket;
				pending.pop_front ();
				lock.unlock ();
				current_l.callback (work);
				lock.lock ();
			}
			else
			{
				// A different thread found a solution
			}
		}
		else
		{
			// Wait for a work request
			producer_condition.wait (lock);
		}
	}
}

void lumex::work_pool::cancel (lumex::root const & root_a)
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	if (!done)
	{
		if (!pending.empty ())
		{
			if (pending.front ().item == root_a)
			{
				++ticket;
			}
		}
		pending.remove_if ([&root_a] (decltype (pending)::value_type const & item_a) {
			bool result{ false };
			if (item_a.item == root_a)
			{
				if (item_a.callback)
				{
					item_a.callback (std::nullopt);
				}
				result = true;
			}
			return result;
		});
	}
}

void lumex::work_pool::stop ()
{
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		done = true;
		++ticket;
	}
	producer_condition.notify_all ();
}

void lumex::work_pool::generate (lumex::work_version const version_a, lumex::root const & root_a, uint64_t difficulty_a, std::function<void (std::optional<uint64_t> const &)> callback_a)
{
	debug_assert (!root_a.is_zero ());
	if (!threads.empty ())
	{
		{
			lumex::lock_guard<lumex::mutex> lock{ mutex };
			pending.emplace_back (version_a, root_a, difficulty_a, callback_a);
		}
		producer_condition.notify_all ();
	}
	else if (callback_a)
	{
		callback_a (std::nullopt);
	}
}

std::optional<uint64_t> lumex::work_pool::generate (lumex::root const & root_a)
{
	debug_assert (network_constants.is_dev_network ());
	return generate (lumex::work_version::work_1, root_a, network_constants.work.base);
}

std::optional<uint64_t> lumex::work_pool::generate (lumex::root const & root_a, uint64_t difficulty_a)
{
	debug_assert (network_constants.is_dev_network ());
	return generate (lumex::work_version::work_1, root_a, difficulty_a);
}

std::optional<uint64_t> lumex::work_pool::generate (lumex::work_version const version_a, lumex::root const & root_a, uint64_t difficulty_a)
{
	std::optional<uint64_t> result;
	if (!threads.empty ())
	{
		std::promise<std::optional<uint64_t>> work;
		std::future<std::optional<uint64_t>> future = work.get_future ();
		generate (version_a, root_a, difficulty_a, [&work] (std::optional<uint64_t> work_a) {
			work.set_value (work_a);
		});
		result = future.get ().value ();
	}
	return result;
}

size_t lumex::work_pool::size ()
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	return pending.size ();
}

lumex::container_info lumex::work_pool::container_info () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	lumex::container_info info;
	info.put ("pending", pending);
	info.add ("work_observers", work_observers.container_info ());
	return info;
}
