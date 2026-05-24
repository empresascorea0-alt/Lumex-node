#include <lumex/lib/logging.hpp>
#include <lumex/lib/network_formatting.hpp>
#include <lumex/lib/thread_roles.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/peer_history.hpp>
#include <lumex/node/transport/channel.hpp>
#include <lumex/store/ledger/peer.hpp>
#include <lumex/store/ledger_store.hpp>

lumex::peer_history::peer_history (lumex::peer_history_config const & config_a, lumex::store::ledger_store & store_a, lumex::network & network_a, lumex::logger & logger_a, lumex::stats & stats_a) :
	config{ config_a },
	store{ store_a },
	network{ network_a },
	logger{ logger_a },
	stats{ stats_a }
{
}

lumex::peer_history::~peer_history ()
{
	debug_assert (!thread.joinable ());
}

void lumex::peer_history::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread ([this] {
		lumex::thread_role::set (lumex::thread_role::name::peer_history);
		run ();
	});
}

void lumex::peer_history::stop ()
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

bool lumex::peer_history::exists (lumex::endpoint const & endpoint) const
{
	auto transaction = store.tx_begin_read ();
	return store.peer.exists (transaction, endpoint);
}

size_t lumex::peer_history::size () const
{
	auto transaction = store.tx_begin_read ();
	return store.peer.count (transaction);
}

void lumex::peer_history::trigger ()
{
	condition.notify_all ();
}

void lumex::peer_history::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait_for (lock, config.check_interval, [this] { return stopped.load (); });
		if (!stopped)
		{
			stats.inc (lumex::stat::type::peer_history, lumex::stat::detail::loop);

			lock.unlock ();

			run_one ();

			lock.lock ();
		}
	}
}

void lumex::peer_history::run_one ()
{
	auto live_peers = network.list ();
	auto transaction = store.tx_begin_write ();

	// Add or update live peers
	for (auto const & peer : live_peers)
	{
		auto const endpoint = peer->get_peering_endpoint ();
		bool const exists = store.peer.exists (transaction, endpoint);
		store.peer.put (transaction, endpoint, lumex::milliseconds_since_epoch ());
		if (!exists)
		{
			stats.inc (lumex::stat::type::peer_history, lumex::stat::detail::inserted);
			logger.debug (lumex::log::type::peer_history, "Saved new peer: {}", endpoint);
		}
		else
		{
			stats.inc (lumex::stat::type::peer_history, lumex::stat::detail::updated);
		}
	}

	// Erase old peers
	auto const now = std::chrono::system_clock::now ();
	auto const cutoff = now - config.erase_cutoff;

	std::deque<lumex::store::ledger::peer_view::iterator::value_type> to_remove;

	for (auto it = store.peer.begin (transaction); it != store.peer.end (transaction); ++it)
	{
		auto const [endpoint, timestamp_millis] = *it;
		auto timestamp = lumex::from_milliseconds_since_epoch (timestamp_millis);
		if (timestamp > now || timestamp < cutoff)
		{
			to_remove.push_back (*it);

			stats.inc (lumex::stat::type::peer_history, lumex::stat::detail::erased);
			logger.debug (lumex::log::type::peer_history, "Erased peer: {} (not seen for {}s)",
			endpoint.endpoint (),
			lumex::log::seconds_delta (timestamp));
		}
	}

	// Remove entries after iterating to avoid iterator invalidation
	for (auto const & entry : to_remove)
	{
		store.peer.del (transaction, entry.first);
	}
}

std::vector<lumex::endpoint> lumex::peer_history::peers () const
{
	auto transaction = store.tx_begin_read ();
	std::vector<lumex::endpoint> peers;
	for (auto it = store.peer.begin (transaction); it != store.peer.end (transaction); ++it)
	{
		auto const [endpoint, timestamp_millis] = *it;
		peers.push_back (endpoint.endpoint ());
	}
	return peers;
}

/*
 * peer_history_config
 */

lumex::peer_history_config::peer_history_config (lumex::network_constants const & network)
{
	if (network.is_dev_network ())
	{
		check_interval = 1s;
		erase_cutoff = 10s;
	}
}

lumex::error lumex::peer_history_config::serialize (lumex::tomlconfig & toml) const
{
	// TODO: Serialization / deserialization
	return toml.get_error ();
}

lumex::error lumex::peer_history_config::deserialize (lumex::tomlconfig & toml)
{
	// TODO: Serialization / deserialization
	return toml.get_error ();
}
