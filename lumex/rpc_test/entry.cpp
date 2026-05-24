#include <lumex/lib/files.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/memory.hpp>
#include <lumex/node/endpoint.hpp>

#include <gtest/gtest.h>

namespace lumex
{
namespace test
{
	void cleanup_dev_directories_on_exit ();
}
void force_lumex_dev_network ();
}

int main (int argc, char ** argv)
{
	lumex::initialize_file_descriptor_limit ();
	lumex::logger::initialize_for_tests (lumex::log_config::tests_default ());
	lumex::force_lumex_dev_network ();
	lumex::set_use_memory_pools (false);
	lumex::node_singleton_memory_pool_purge_guard cleanup_guard;
	testing::InitGoogleTest (&argc, argv);
	auto res = RUN_ALL_TESTS ();
	lumex::test::cleanup_dev_directories_on_exit ();
	return res;
}
