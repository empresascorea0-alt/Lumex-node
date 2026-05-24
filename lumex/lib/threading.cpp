#include <lumex/lib/config.hpp>
#include <lumex/lib/env.hpp>
#include <lumex/lib/thread_roles.hpp>
#include <lumex/lib/threading.hpp>

#include <thread>

/*
 * thread_attributes
 */

boost::thread::attributes lumex::thread_attributes::get_default ()
{
	boost::thread::attributes attrs;
	attrs.set_stack_size (8000000); // 8MB
	return attrs;
}

unsigned lumex::hardware_concurrency ()
{
	static auto const concurrency = [] () {
		if (auto value = lumex::env::get<unsigned> ("LUMEX_HARDWARE_CONCURRENCY"))
		{
			std::cerr << "Hardware concurrency overridden by LUMEX_HARDWARE_CONCURRENCY environment variable: " << *value << std::endl;
			return *value;
		}
		return std::thread::hardware_concurrency ();
	}();
	release_assert (concurrency > 0, "configured hardware concurrency must be non zero");
	return concurrency;
}

bool lumex::join_or_pass (std::thread & thread)
{
	if (thread.joinable ())
	{
		thread.join ();
		return true;
	}
	else
	{
		return false;
	}
}
