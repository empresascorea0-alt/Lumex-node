#include <lumex/lib/formatting.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/thread_roles.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/election_behavior.hpp>
#include <lumex/node/monitor.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/online_reps.hpp>
#include <lumex/node/repcrawler.hpp>
#include <lumex/node/transport/tcp_listener.hpp>
#include <lumex/secure/ledger.hpp>

lumex::monitor::monitor (lumex::monitor_config const & config_a, lumex::node & node_a) :
	config{ config_a },
	node{ node_a },
	logger{ node_a.logger }
{
}

lumex::monitor::~monitor ()
{
	debug_assert (!thread.joinable ());
}

void lumex::monitor::start ()
{
	if (!config.enable)
	{
		return;
	}

	thread = std::thread ([this] () {
		lumex::thread_role::set (lumex::thread_role::name::monitor);
		run ();
	});
}

void lumex::monitor::stop ()
{
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

void lumex::monitor::run ()
{
	std::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		run_one ();
		condition.wait_until (lock, std::chrono::steady_clock::now () + config.interval, [this] { return stopped; });
	}
}

void lumex::monitor::run_one ()
{
	auto const now = std::chrono::steady_clock::now ();

	auto blocks_cemented = node.ledger.cemented_count ();
	auto blocks_total = node.ledger.block_count ();

	// TODO: Maybe emphasize somehow that confirmed doesn't need to be equal to total; backlog is OK
	logger.info (lumex::log::type::monitor, "Blocks confirmed: {} | total: {} (backlog: {})",
	blocks_cemented,
	blocks_total,
	blocks_total - blocks_cemented);

	// Wait for node to warm up before logging rates
	if (last_time != std::chrono::steady_clock::time_point{})
	{
		auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds> (now - last_time).count ();
		debug_assert (elapsed_seconds > 0);

		// Cast to signed type to correctly handle negative differences (e.g., from rollbacks)
		auto cemented_diff = static_cast<std::int64_t> (blocks_cemented) - static_cast<std::int64_t> (last_blocks_cemented);
		auto total_diff = static_cast<std::int64_t> (blocks_total) - static_cast<std::int64_t> (last_blocks_total);

		// Calculate the rates
		auto blocks_confirmed_rate = static_cast<double> (cemented_diff) / elapsed_seconds;
		auto blocks_checked_rate = static_cast<double> (total_diff) / elapsed_seconds;

		logger.info (lumex::log::type::monitor, "Blocks rate (avg over {}s): confirmed {:.2f}/s | total {:.2f}/s",
		elapsed_seconds,
		blocks_confirmed_rate,
		blocks_checked_rate);
	}

	logger.info (lumex::log::type::monitor, "Peers: {} (realtime: {} | bootstrap: {}) (inbound: {} | outbound: {})",
	node.network.size (),
	node.tcp_listener.realtime_count (),
	node.tcp_listener.bootstrap_count (),
	node.tcp_listener.connection_count (lumex::transport::tcp_listener::connection_type::inbound),
	node.tcp_listener.connection_count (lumex::transport::tcp_listener::connection_type::outbound));

	auto const quorum = node.online_reps.delta ();
	auto const stake_online = node.online_reps.online ();
	auto const stake_peered = node.rep_crawler.total_weight ();

	logger.info (lumex::log::type::monitor, "Quorum: {} (stake peered: {} | stake online: {})",
	lumex::log::as_lumex (quorum),
	lumex::log::as_lumex (stake_peered),
	lumex::log::as_lumex (stake_online));

	logger.info (lumex::log::type::monitor, "Elections active: {} (priority: {} | hinted: {} | optimistic: {}) of which stale: {}",
	node.active.size (),
	node.active.size (lumex::election_behavior::priority),
	node.active.size (lumex::election_behavior::hinted),
	node.active.size (lumex::election_behavior::optimistic),
	node.active.stale_count ());

	bool const sufficient_stake = stake_peered >= quorum;

	if (!sufficient_stake && node.warmed_up ())
	{
		logger.warn (lumex::log::type::monitor, "Peered stake ({}) is below quorum threshold ({}). The node may not be able to confirm transactions. This is usually caused by NAT, firewall rules, or internet connectivity issues.",
		lumex::log::as_lumex (stake_peered),
		lumex::log::as_lumex (quorum));
	}

	last_time = now;
	last_blocks_cemented = blocks_cemented;
	last_blocks_total = blocks_total;
}

/*
 * monitor_config
 */

lumex::error lumex::monitor_config::serialize (lumex::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Enable or disable periodic node status logging\ntype:bool");
	toml.put ("interval", interval.count (), "Interval between status logs\ntype:seconds");

	return toml.get_error ();
}

lumex::error lumex::monitor_config::deserialize (lumex::tomlconfig & toml)
{
	toml.get ("enable", enable);
	toml.get_duration ("interval", interval);

	return toml.get_error ();
}