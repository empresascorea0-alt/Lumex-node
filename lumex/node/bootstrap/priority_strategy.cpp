#include <lumex/lib/stats_enums.hpp>
#include <lumex/lib/thread_roles.hpp>
#include <lumex/node/bootstrap/priority_strategy.hpp>
#include <lumex/node/nodeconfig.hpp>

namespace lumex::bootstrap
{
priority_strategy::priority_strategy (bootstrap_context & ctx_a) :
	ctx{ ctx_a }
{
}

void priority_strategy::start ()
{
	debug_assert (!thread.joinable ());
	thread = std::thread ([this] () {
		lumex::thread_role::set (lumex::thread_role::name::bootstrap);
		run ();
	});
}

void priority_strategy::stop ()
{
	lumex::join_or_pass (thread);
}

void priority_strategy::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ ctx.mutex };
	while (!ctx.stopped)
	{
		lock.unlock ();
		ctx.stats.inc (lumex::stat::type::bootstrap, lumex::stat::detail::loop);
		run_one ();
		lock.lock ();
	}
}

void priority_strategy::run_one ()
{
	ctx.wait_block_processor ();
	auto channel = ctx.wait_channel ();
	if (!channel)
	{
		return;
	}
	auto [account, priority, fails] = wait_priority ();
	if (account.is_zero ())
	{
		return;
	}

	// Decide how many blocks to request
	size_t const min_pull_count = 2;
	auto pull_count = std::clamp (static_cast<size_t> (priority), min_pull_count, lumex::bootstrap_server::max_blocks);

	bool sent = ctx.request (account, pull_count, channel, query_source::priority);

	// Only cooldown accounts that are likely to have more blocks
	// This is to avoid requesting blocks from the same frontier multiple times, before the block processor had a chance to process them
	// Not throttling accounts that are probably up-to-date allows us to evict them from the priority set faster
	if (sent && fails == 0)
	{
		lumex::lock_guard<lumex::mutex> lock{ ctx.mutex };
		ctx.accounts.timestamp_set (account);
	}
}

auto priority_strategy::next_priority () -> priority_result
{
	debug_assert (!ctx.mutex.try_lock ());

	auto next = ctx.accounts.next_priority ([this] (lumex::account const & account) {
		return ctx.count_tags (account, query_source::priority) < 4;
	});
	if (next.account.is_zero ())
	{
		return {};
	}
	ctx.stats.inc (lumex::stat::type::bootstrap_next, lumex::stat::detail::next_priority);
	return next;
}

auto priority_strategy::wait_priority () -> priority_result
{
	priority_result result{};
	ctx.wait ([this, &result] () {
		debug_assert (!ctx.mutex.try_lock ());
		result = next_priority ();
		if (!result.account.is_zero ())
		{
			return true;
		}
		return false;
	});
	return result;
}
}
