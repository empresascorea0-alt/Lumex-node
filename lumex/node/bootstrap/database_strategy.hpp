#pragma once

#include <lumex/node/bootstrap/bootstrap_context.hpp>

#include <thread>

namespace lumex::bootstrap
{
class database_strategy
{
public:
	explicit database_strategy (bootstrap_context & ctx);

	void start ();
	void stop ();
	void run_one (bool should_throttle);

private:
	void run ();

	lumex::account next_database (bool should_throttle);
	lumex::account wait_database (bool should_throttle);

	bootstrap_context & ctx;
	std::thread thread;
};
}
