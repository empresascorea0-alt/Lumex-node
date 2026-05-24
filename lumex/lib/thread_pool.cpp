#include <lumex/lib/container_info.hpp>
#include <lumex/lib/thread_pool.hpp>

lumex::container_info lumex::thread_pool::container_info () const
{
	lumex::container_info info;
	info.put ("tasks", num_tasks);
	info.put ("delayed", num_delayed);
	return info;
}
