#pragma once

#include <lumex/lib/config.hpp>
#include <lumex/lib/constants.hpp>
#include <lumex/lib/locks.hpp>
#include <lumex/node/openclconfig.hpp>
#include <lumex/node/xorshift.hpp>

#include <atomic>
#include <mutex>
#include <optional>
#include <vector>

#ifdef __APPLE__
#define CL_SILENCE_DEPRECATION
#include <OpenCL/opencl.h>
#else
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#include <CL/cl.h>
#endif

namespace lumex
{
extern bool opencl_loaded;
class logger;

class opencl_platform
{
public:
	cl_platform_id platform;
	std::vector<cl_device_id> devices;
};

class opencl_environment
{
public:
	opencl_environment (bool &);
	void dump (std::ostream & stream);
	std::vector<lumex::opencl_platform> platforms;
};

class root;
class work_pool;

class opencl_work
{
public:
	opencl_work (bool &, lumex::opencl_config const &, lumex::opencl_environment &, lumex::logger &, lumex::work_thresholds & work);
	~opencl_work ();
	std::optional<uint64_t> generate_work (lumex::work_version const, lumex::root const &, uint64_t const);
	std::optional<uint64_t> generate_work (lumex::work_version const, lumex::root const &, uint64_t const, std::atomic<int> &);
	static std::unique_ptr<opencl_work> create (bool, lumex::opencl_config const &, lumex::logger &, lumex::work_thresholds & work);
	lumex::opencl_config const & config;
	lumex::mutex mutex;
	cl_context context;
	cl_mem attempt_buffer;
	cl_mem result_buffer;
	cl_mem item_buffer;
	cl_mem difficulty_buffer;
	cl_program program;
	cl_kernel kernel;
	cl_command_queue queue;
	lumex::xorshift1024star rand;
	lumex::logger & logger;
	lumex::work_thresholds & work;
};
}
