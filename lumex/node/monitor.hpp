#pragma once

#include <lumex/lib/locks.hpp>
#include <lumex/node/fwd.hpp>

#include <chrono>
#include <thread>

using namespace std::chrono_literals;

namespace lumex
{
class monitor_config final
{
public:
	lumex::error deserialize (lumex::tomlconfig &);
	lumex::error serialize (lumex::tomlconfig &) const;

public:
	bool enable{ true };
	std::chrono::seconds interval{ 60s };
};

class monitor final
{
public:
	monitor (monitor_config const &, lumex::node &);
	~monitor ();

	void start ();
	void stop ();

private: // Dependencies
	monitor_config const & config;
	lumex::node & node;
	lumex::logger & logger;

private:
	void run ();
	void run_one ();

	std::chrono::steady_clock::time_point last_time{};

	size_t last_blocks_cemented{ 0 };
	size_t last_blocks_total{ 0 };

	bool stopped{ false };
	lumex::condition_variable condition;
	mutable lumex::mutex mutex;
	std::thread thread;
};
}