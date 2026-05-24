#include <lumex/lib/stats_enums.hpp>
#include <lumex/lib/thread_roles.hpp>
#include <lumex/node/bootstrap/database_strategy.hpp>
#include <lumex/node/nodeconfig.hpp>

namespace lumex::bootstrap
{
database_strategy::database_strategy (bootstrap_context & ctx_a) :
	ctx{ ctx_a }
{
}

void database_strategy::start ()
{
	debug_assert (!thread.joinable ());
	thread = std::thread ([this] () {
		lumex::thread_role::set (lumex::thread_role::name::bootstrap_database_scan);
		run ();
	});
}

void database_strategy::stop ()
{
	lumex::join_or_pass (thread);
}

void database_strategy::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ ctx.mutex };
	while (!ctx.stopped)
	{
		// Avoid high churn rate of database requests
		bool should_throttle = !ctx.database_scan.warmed_up () && ctx.throttle.throttled ();
		lock.unlock ();
		ctx.stats.inc (lumex::stat::type::bootstrap, lumex::stat::detail::loop_database);
		run_one (should_throttle);
		lock.lock ();
	}
}

void database_strategy::run_one (bool should_throttle)
{
	ctx.wait_block_processor ();
	auto channel = ctx.wait_channel ();
	if (!channel)
	{
		return;
	}
	auto account = wait_database (should_throttle);
	if (account.is_zero ())
	{
		return;
	}
	ctx.request (account, 2, channel, query_source::database);
}

lumex::account database_strategy::next_database (bool should_throttle)
{
	debug_assert (!ctx.mutex.try_lock ());
	debug_assert (ctx.config.database_warmup_ratio > 0);

	// Throttling increases the weight of database requests
	if (!ctx.database_limiter.should_pass (should_throttle ? ctx.config.database_warmup_ratio : 1))
	{
		return { 0 };
	}
	auto account = ctx.database_scan.next ([this] (lumex::account const & account) {
		return ctx.count_tags (account, query_source::database) == 0;
	});
	if (account.is_zero ())
	{
		return { 0 };
	}
	ctx.stats.inc (lumex::stat::type::bootstrap_next, lumex::stat::detail::next_database);
	return account;
}

lumex::account database_strategy::wait_database (bool should_throttle)
{
	lumex::account result{ 0 };
	ctx.wait ([this, &result, should_throttle] () {
		debug_assert (!ctx.mutex.try_lock ());
		result = next_database (should_throttle);
		if (!result.is_zero ())
		{
			return true;
		}
		return false;
	});
	return result;
}
}
