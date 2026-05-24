#include <lumex/lib/logging.hpp>
#include <lumex/lib/stats_enums.hpp>
#include <lumex/lib/thread_roles.hpp>
#include <lumex/messages/asc_pull.hpp>
#include <lumex/node/bootstrap/dependency_strategy.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/transport/formatting.hpp>

using namespace std::chrono_literals;

namespace lumex::bootstrap
{
dependency_strategy::dependency_strategy (bootstrap_context & ctx_a) :
	ctx{ ctx_a }
{
}

void dependency_strategy::start ()
{
	debug_assert (!thread.joinable ());
	thread = std::thread ([this] () {
		lumex::thread_role::set (lumex::thread_role::name::bootstrap_dependency_walker);
		run ();
	});

	debug_assert (!sync_thread.joinable ());
	sync_thread = std::thread ([this] () {
		lumex::thread_role::set (lumex::thread_role::name::bootstrap_dependency_sync);
		run_sync ();
	});
}

void dependency_strategy::stop ()
{
	lumex::join_or_pass (thread);
	lumex::join_or_pass (sync_thread);
}

void dependency_strategy::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ ctx.mutex };
	while (!ctx.stopped)
	{
		lock.unlock ();
		ctx.stats.inc (lumex::stat::type::bootstrap, lumex::stat::detail::loop_dependencies);
		run_one ();
		lock.lock ();
	}
}

void dependency_strategy::run_one ()
{
	// No need to wait for block_processor, as we are not processing blocks
	auto channel = ctx.wait_channel ();
	if (!channel)
	{
		return;
	}
	auto blocking = wait_blocking ();
	if (blocking.is_zero ())
	{
		return;
	}
	request_info (blocking, channel);
}

lumex::block_hash dependency_strategy::next_blocking ()
{
	debug_assert (!ctx.mutex.try_lock ());

	auto blocking = ctx.accounts.next_blocking ([this] (lumex::block_hash const & hash) {
		return ctx.count_tags (hash, query_source::dependencies) == 0;
	});
	if (blocking.is_zero ())
	{
		return { 0 };
	}
	ctx.stats.inc (lumex::stat::type::bootstrap_next, lumex::stat::detail::next_blocking);
	return blocking;
}

lumex::block_hash dependency_strategy::wait_blocking ()
{
	lumex::block_hash result{ 0 };
	ctx.wait ([this, &result] () {
		result = next_blocking ();
		if (!result.is_zero ())
		{
			return true;
		}
		return false;
	});
	return result;
}

bool dependency_strategy::request_info (lumex::block_hash hash, std::shared_ptr<lumex::transport::channel> const & channel)
{
	async_tag tag{};
	tag.type = query_type::account_info_by_hash;
	tag.source = query_source::dependencies;
	tag.hash = hash;

	dependency_tag_payload payload{};
	payload.start = hash;
	tag.payload = payload;

	// Build the message
	lumex::messages::asc_pull_req message{ ctx.network_constants };
	message.id = tag.id;
	message.type = lumex::messages::asc_pull_type::account_info;

	lumex::messages::asc_pull_req::account_info_payload msg_pld;
	msg_pld.target_type = lumex::messages::asc_pull_req::hash_type::block; // Query account info by block hash
	msg_pld.target = hash;
	message.payload = msg_pld;
	message.update_header ();

	ctx.logger.debug (lumex::log::type::bootstrap, "Requesting account info for: {} from: {}", hash, channel);

	return ctx.send (channel, std::move (message), tag);
}

bool dependency_strategy::process (lumex::messages::asc_pull_ack::account_info_payload const & response, async_tag const & tag)
{
	debug_assert (!ctx.mutex.try_lock ());
	debug_assert (tag.type == query_type::account_info_by_hash);
	debug_assert (!tag.hash.is_zero ());

	if (response.account.is_zero ())
	{
		ctx.stats.inc (lumex::stat::type::bootstrap_process, lumex::stat::detail::account_info_empty);
		return true; // OK, but nothing to do
	}

	ctx.stats.inc (lumex::stat::type::bootstrap_process, lumex::stat::detail::account_info);

	// Prioritize account containing the dependency
	ctx.accounts.dependency_update (tag.hash, response.account);
	ctx.accounts.priority_set (response.account, account_sets_index::priority_cutoff); // Use the lowest possible priority here

	return true; // OK, no way to verify the response
}

void dependency_strategy::run_sync ()
{
	lumex::unique_lock<lumex::mutex> lock{ ctx.mutex };
	while (!ctx.stopped)
	{
		// Reinsert known dependencies into the priority set
		ctx.stats.inc (lumex::stat::type::bootstrap, lumex::stat::detail::sync_dependencies);
		auto synced = ctx.accounts.sync_dependencies ();
		ctx.logger.debug (lumex::log::type::bootstrap, "Synced {} dependencies", synced);
		ctx.condition.wait_for (lock, lumex::is_dev_run () ? 500ms : 60s, [this] () { return ctx.stopped; });
	}
}
}
