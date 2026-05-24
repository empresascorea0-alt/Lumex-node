#pragma once

#include <lumex/lib/config.hpp>
#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/observer_set.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/node/openclwork.hpp>

#include <atomic>
#include <list>
#include <memory>
#include <optional>
#include <thread>

namespace lumex
{
std::string to_string (lumex::work_version const version_a);

// type of function that does the work generation with an optional return value
using opencl_work_func_t = std::function<std::optional<uint64_t> (lumex::work_version const, lumex::root const &, uint64_t, std::atomic<int> &)>;

class block;
class block_details;
enum class block_type : uint8_t;

class opencl_work;
class work_item final
{
public:
	work_item (lumex::work_version const version_a, lumex::root const & item_a, uint64_t difficulty_a, std::function<void (std::optional<uint64_t> const &)> const & callback_a) :
		version (version_a), item (item_a), difficulty (difficulty_a), callback (callback_a)
	{
	}
	lumex::work_version const version;
	lumex::root const item;
	uint64_t const difficulty;
	std::function<void (std::optional<uint64_t> const &)> const callback;
};
class work_pool final
{
public:
	work_pool (lumex::network_constants & network_constants, unsigned, std::chrono::lumexseconds = std::chrono::lumexseconds (0), lumex::opencl_work_func_t = nullptr);
	~work_pool ();
	void loop (uint64_t);
	void stop ();
	void cancel (lumex::root const &);
	void generate (lumex::work_version const, lumex::root const &, uint64_t, std::function<void (std::optional<uint64_t> const &)>);
	std::optional<uint64_t> generate (lumex::work_version const, lumex::root const &, uint64_t);
	// For tests only
	std::optional<uint64_t> generate (lumex::root const &);
	std::optional<uint64_t> generate (lumex::root const &, uint64_t);
	size_t size ();
	lumex::network_constants & network_constants;
	std::atomic<int> ticket;
	bool done;
	std::vector<std::thread> threads;
	std::list<lumex::work_item> pending;
	mutable lumex::mutex mutex{ mutex_identifier (mutexes::work_pool) };
	lumex::condition_variable producer_condition;
	std::chrono::lumexseconds pow_rate_limiter;
	lumex::opencl_work_func_t opencl;
	lumex::observer_set<bool> work_observers;

	lumex::container_info container_info () const;
};
}
