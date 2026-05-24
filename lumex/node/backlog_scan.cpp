#include <lumex/lib/saturate.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/thread_roles.hpp>
#include <lumex/lib/threading.hpp>
#include <lumex/node/backlog_scan.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/scheduler/priority.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/store/ledger/account.hpp>
#include <lumex/store/ledger/confirmation_height.hpp>

lumex::backlog_scan::backlog_scan (backlog_scan_config const & config_a, lumex::ledger & ledger_a, lumex::stats & stats_a) :
	config{ config_a },
	ledger{ ledger_a },
	stats{ stats_a },
	limiter{ config.rate_limit }
{
}

lumex::backlog_scan::~backlog_scan ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void lumex::backlog_scan::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		lumex::thread_role::set (lumex::thread_role::name::backlog_scan);
		run ();
	} };
}

void lumex::backlog_scan::stop ()
{
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		stopped = true;
	}
	notify ();
	lumex::join_or_pass (thread);
}

void lumex::backlog_scan::trigger ()
{
	{
		lumex::unique_lock<lumex::mutex> lock{ mutex };
		triggered = true;
	}
	notify ();
}

void lumex::backlog_scan::notify ()
{
	condition.notify_all ();
}

bool lumex::backlog_scan::predicate () const
{
	return triggered || config.enable;
}

void lumex::backlog_scan::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		if (predicate ())
		{
			stats.inc (lumex::stat::type::backlog_scan, lumex::stat::detail::loop);
			triggered = false;
			populate_backlog (lock); // Does a single iteration over all accounts
			debug_assert (lock.owns_lock ());
		}
		else
		{
			condition.wait (lock, [this] () {
				return stopped || predicate ();
			});
		}
	}
}

void lumex::backlog_scan::populate_backlog (lumex::unique_lock<lumex::mutex> & lock)
{
	uint64_t total = 0;

	lumex::account next = 0;
	bool done = false;
	while (!stopped && !done)
	{
		// Wait for the rate limiter
		while (!limiter.should_pass (config.batch_size))
		{
			std::chrono::milliseconds const wait_time{ 1000 / std::max ((config.rate_limit / config.batch_size), size_t{ 1 }) / 2 };
			condition.wait_for (lock, std::max (wait_time, 10ms));
			if (stopped)
			{
				return;
			}
		}

		lock.unlock ();

		std::deque<activated_info> scanned;
		std::deque<activated_info> activated;
		{
			auto transaction = ledger.tx_begin_read ();

			auto it = ledger.store.account.begin (transaction, next);
			auto const end = ledger.store.account.end (transaction);

			for (size_t count = 0; it != end && count < config.batch_size; ++it, ++count, ++total)
			{
				stats.inc (lumex::stat::type::backlog_scan, lumex::stat::detail::total);

				auto const [account, account_info] = *it;
				auto const maybe_conf_info = ledger.store.confirmation_height.get (transaction, account);
				auto const conf_info = maybe_conf_info.value_or (lumex::confirmation_height_info{});

				activated_info info{ account, account_info, conf_info };

				scanned.push_back (info);
				if (conf_info.height < account_info.block_count)
				{
					activated.push_back (info);
				}

				next = inc_sat (account.number ());
			}

			done = (it == end);
		}

		stats.add (lumex::stat::type::backlog_scan, lumex::stat::detail::scanned, scanned.size ());
		stats.add (lumex::stat::type::backlog_scan, lumex::stat::detail::activated, activated.size ());

		// Notify about scanned and activated accounts without holding database transaction
		batch_scanned.notify (scanned);
		batch_activated.notify (activated);

		lock.lock ();
	}
}

lumex::container_info lumex::backlog_scan::container_info () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	lumex::container_info info;
	info.put ("limiter", limiter.size ());
	return info;
}

/*
 * backlog_scan_config
 */

lumex::error lumex::backlog_scan_config::serialize (lumex::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Control if ongoing backlog population is enabled. If not, backlog population can still be triggered by RPC \ntype:bool");
	toml.put ("batch_size", batch_size, "Size of a single batch. Larger batches reduce overhead, but may put more pressure on other node components. \ntype:uint");
	toml.put ("rate_limit", rate_limit, "Number of accounts per second to process when doing backlog population scan. Increasing this value will help unconfirmed frontiers get into election prioritization queue faster. Use 0 to process as fast as possible, but be aware that it may consume a lot of resources. \ntype:uint");

	return toml.get_error ();
}

lumex::error lumex::backlog_scan_config::deserialize (lumex::tomlconfig & toml)
{
	toml.get ("enable", enable);
	toml.get ("batch_size", batch_size);
	toml.get ("rate_limit", rate_limit);

	return toml.get_error ();
}
