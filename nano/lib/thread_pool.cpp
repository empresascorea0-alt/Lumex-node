#include <nano/lib/container_info.hpp>
#include <nano/lib/thread_pool.hpp>

nano::container_info nano::thread_pool::container_info () const
{
	nano::container_info info;
	info.put ("tasks", num_tasks);
	info.put ("delayed", num_delayed);
	return info;
}
