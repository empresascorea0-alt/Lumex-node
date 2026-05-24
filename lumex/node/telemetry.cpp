#include <lumex/lib/blocks.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/thread_roles.hpp>
#include <lumex/lib/threading.hpp>
#include <lumex/messages/telemetry.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/node_observers.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/telemetry.hpp>
#include <lumex/node/transport/transport.hpp>
#include <lumex/secure/ledger.hpp>

#include <boost/algorithm/string.hpp>

#include <algorithm>
#include <cstdint>
#include <future>
#include <numeric>
#include <set>

using namespace std::chrono_literals;

lumex::telemetry_config::telemetry_config (lumex::node_flags const & flags) :
	enable_ongoing_broadcasts{ !flags.disable_providing_telemetry_metrics }
{
}

lumex::telemetry::telemetry (lumex::node_flags const & flags_a, lumex::node & node_a, lumex::network & network_a, lumex::node_observers & observers_a, lumex::network_params & network_params_a, lumex::stats & stats_a) :
	config{ flags_a },
	node{ node_a },
	network{ network_a },
	observers{ observers_a },
	network_params{ network_params_a },
	stats{ stats_a }
{
}

lumex::telemetry::~telemetry ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void lumex::telemetry::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread ([this] () {
		lumex::thread_role::set (lumex::thread_role::name::telemetry);
		run ();
	});
}

void lumex::telemetry::stop ()
{
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	lumex::join_or_pass (thread);
}

bool lumex::telemetry::verify (const lumex::messages::telemetry_ack & telemetry, const std::shared_ptr<lumex::transport::channel> & channel) const
{
	if (telemetry.is_empty_payload ())
	{
		stats.inc (lumex::stat::type::telemetry, lumex::stat::detail::empty_payload);
		return false;
	}

	// Check if telemetry node id matches channel node id
	if (telemetry.data.node_id != channel->get_node_id ())
	{
		stats.inc (lumex::stat::type::telemetry, lumex::stat::detail::node_id_mismatch);
		return false;
	}

	// Check whether data is signed by node id presented in telemetry message
	if (telemetry.data.validate_signature ()) // Returns false when signature OK
	{
		stats.inc (lumex::stat::type::telemetry, lumex::stat::detail::invalid_signature);
		return false;
	}

	if (telemetry.data.genesis_block != network_params.ledger.genesis->hash ())
	{
		network.exclude (channel);

		stats.inc (lumex::stat::type::telemetry, lumex::stat::detail::genesis_mismatch);
		return false;
	}

	return true; // Telemetry is OK
}

void lumex::telemetry::process (const lumex::messages::telemetry_ack & telemetry, const std::shared_ptr<lumex::transport::channel> & channel)
{
	if (!verify (telemetry, channel))
	{
		return;
	}

	lumex::unique_lock<lumex::mutex> lock{ mutex };

	if (auto it = telemetries.get<tag_channel> ().find (channel); it != telemetries.get<tag_channel> ().end ())
	{
		stats.inc (lumex::stat::type::telemetry, lumex::stat::detail::update);

		telemetries.get<tag_channel> ().modify (it, [&telemetry, &channel] (auto & entry) {
			entry.data = telemetry.data;
			entry.last_updated = std::chrono::steady_clock::now ();
		});
	}
	else
	{
		stats.inc (lumex::stat::type::telemetry, lumex::stat::detail::insert);
		telemetries.get<tag_channel> ().insert ({ channel, telemetry.data, std::chrono::steady_clock::now () });

		if (telemetries.size () > max_size)
		{
			stats.inc (lumex::stat::type::telemetry, lumex::stat::detail::overfill);
			telemetries.get<tag_sequenced> ().pop_front (); // Erase oldest entry
		}
	}

	lock.unlock ();

	observers.telemetry.notify (telemetry.data, channel);

	stats.inc (lumex::stat::type::telemetry, lumex::stat::detail::process);
}

void lumex::telemetry::trigger ()
{
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		triggered = true;
	}
	condition.notify_all ();
}

std::size_t lumex::telemetry::size () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return telemetries.size ();
}

bool lumex::telemetry::request_predicate () const
{
	debug_assert (!mutex.try_lock ());

	if (triggered)
	{
		return true;
	}
	if (config.enable_ongoing_requests)
	{
		return last_request + network_params.network.telemetry_request_interval < std::chrono::steady_clock::now ();
	}
	return false;
}

bool lumex::telemetry::broadcast_predicate () const
{
	debug_assert (!mutex.try_lock ());

	if (config.enable_ongoing_broadcasts)
	{
		return last_broadcast + network_params.network.telemetry_broadcast_interval < std::chrono::steady_clock::now ();
	}
	return false;
}

void lumex::telemetry::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (lumex::stat::type::telemetry, lumex::stat::detail::loop);

		cleanup ();

		if (request_predicate ())
		{
			triggered = false;
			lock.unlock ();

			run_requests ();

			lock.lock ();
			last_request = std::chrono::steady_clock::now ();
		}

		if (broadcast_predicate ())
		{
			lock.unlock ();

			run_broadcasts ();

			lock.lock ();
			last_broadcast = std::chrono::steady_clock::now ();
		}

		condition.wait_for (lock, std::min (network_params.network.telemetry_request_interval, network_params.network.telemetry_broadcast_interval) / 2);
	}
}

void lumex::telemetry::run_requests ()
{
	auto peers = network.list ();

	for (auto & channel : peers)
	{
		request (channel);
	}
}

void lumex::telemetry::request (std::shared_ptr<lumex::transport::channel> const & channel)
{
	stats.inc (lumex::stat::type::telemetry, lumex::stat::detail::request);

	lumex::messages::telemetry_req message{ network_params.network };
	channel->send (message, lumex::transport::traffic_type::telemetry);
}

void lumex::telemetry::run_broadcasts ()
{
	auto telemetry = node.local_telemetry ();
	auto peers = network.list ();

	for (auto & channel : peers)
	{
		broadcast (channel, telemetry);
	}
}

void lumex::telemetry::broadcast (std::shared_ptr<lumex::transport::channel> const & channel, const lumex::messages::telemetry_data & telemetry)
{
	stats.inc (lumex::stat::type::telemetry, lumex::stat::detail::broadcast);

	lumex::messages::telemetry_ack message{ network_params.network, telemetry };
	channel->send (message, lumex::transport::traffic_type::telemetry);
}

void lumex::telemetry::cleanup ()
{
	debug_assert (!mutex.try_lock ());

	erase_if (telemetries, [this] (entry const & entry) {
		// Remove if telemetry data is stale
		if (!check_timeout (entry))
		{
			stats.inc (lumex::stat::type::telemetry, lumex::stat::detail::erase_stale);
			return true; // Erase
		}
		if (!entry.channel->alive ())
		{
			stats.inc (lumex::stat::type::telemetry, lumex::stat::detail::erase_dead);
			return true; // Erase
		}
		return false; // Do not erase
	});
}

bool lumex::telemetry::check_timeout (const entry & entry) const
{
	return entry.last_updated + network_params.network.telemetry_cache_cutoff >= std::chrono::steady_clock::now ();
}

std::optional<lumex::messages::telemetry_data> lumex::telemetry::get_telemetry (const lumex::endpoint & endpoint) const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	if (auto it = telemetries.get<tag_endpoint> ().find (endpoint); it != telemetries.get<tag_endpoint> ().end ())
	{
		if (check_timeout (*it))
		{
			return it->data;
		}
	}
	return {};
}

std::unordered_map<lumex::endpoint, lumex::messages::telemetry_data> lumex::telemetry::get_all_telemetries () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	std::unordered_map<lumex::endpoint, lumex::messages::telemetry_data> result;
	for (auto const & entry : telemetries)
	{
		if (check_timeout (entry))
		{
			result[entry.endpoint ()] = entry.data;
		}
	}
	return result;
}

lumex::container_info lumex::telemetry::container_info () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	lumex::container_info info;
	info.put ("telemetries", telemetries.size ());
	return info;
}