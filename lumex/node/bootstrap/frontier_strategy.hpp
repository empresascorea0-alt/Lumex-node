#pragma once

#include <lumex/node/bootstrap/bootstrap_context.hpp>

#include <deque>
#include <memory>
#include <thread>
#include <utility>

namespace lumex::bootstrap
{
class frontier_strategy
{
public:
	explicit frontier_strategy (bootstrap_context & ctx);

	void start ();
	void stop ();
	void run_one ();

	bool process (lumex::messages::asc_pull_ack::frontiers_payload const & response, async_tag const & tag);

private:
	void run ();

	lumex::account wait_frontier ();
	bool request_frontiers (lumex::account start, std::shared_ptr<lumex::transport::channel> const & channel);
	verify_result verify (lumex::messages::asc_pull_ack::frontiers_payload const & response, async_tag const & tag) const;
	void process_frontiers (std::deque<std::pair<lumex::account, lumex::block_hash>> const & frontiers);

	bootstrap_context & ctx;
	std::thread thread;
};
}
