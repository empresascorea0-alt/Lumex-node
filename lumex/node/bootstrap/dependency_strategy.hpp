#pragma once

#include <lumex/node/bootstrap/bootstrap_context.hpp>

#include <thread>

namespace lumex::bootstrap
{
class dependency_strategy
{
public:
	explicit dependency_strategy (bootstrap_context & ctx);

	void start ();
	void stop ();
	void run_one ();

	bool process (lumex::messages::asc_pull_ack::account_info_payload const & response, async_tag const & tag);

private:
	void run ();
	void run_sync ();

	lumex::block_hash next_blocking ();
	lumex::block_hash wait_blocking ();
	bool request_info (lumex::block_hash hash, std::shared_ptr<lumex::transport::channel> const & channel);

	bootstrap_context & ctx;
	std::thread thread;
	std::thread sync_thread;
};
}
