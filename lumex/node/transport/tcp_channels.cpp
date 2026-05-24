#include <lumex/lib/formatting.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/network_formatting.hpp>
#include <lumex/messages/keepalive.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/node_observers.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/transport/formatting.hpp>
#include <lumex/node/transport/tcp_channels.hpp>
#include <lumex/node/transport/tcp_listener.hpp>
#include <lumex/node/transport/tcp_server.hpp>

#include <ranges>

/*
 * tcp_channels
 */

lumex::transport::tcp_channels::tcp_channels (lumex::node & node) :
	node{ node }
{
}

lumex::transport::tcp_channels::~tcp_channels ()
{
	debug_assert (channels.empty ());
}

void lumex::transport::tcp_channels::start ()
{
}

void lumex::transport::tcp_channels::stop ()
{
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();

	close ();
}

void lumex::transport::tcp_channels::close ()
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };

	for (auto const & entry : channels)
	{
		entry.socket->close ();
		entry.server->close ();
		entry.channel->close ();
	}

	channels.clear ();
}

bool lumex::transport::tcp_channels::check (const lumex::tcp_endpoint & endpoint, const lumex::account & node_id) const
{
	debug_assert (!mutex.try_lock ());

	if (stopped)
	{
		return false; // Reject
	}

	if (node.network.not_a_peer (lumex::transport::map_tcp_to_endpoint (endpoint), node.config.allow_local_peers))
	{
		node.stats.inc (lumex::stat::type::tcp_channels_rejected, lumex::stat::detail::not_a_peer);
		node.logger.debug (lumex::log::type::tcp_channels, "Rejected invalid endpoint channel: {}", endpoint);

		return false; // Reject
	}

	bool has_duplicate = std::any_of (channels.begin (), channels.end (), [&endpoint, &node_id] (auto const & channel) {
		if (lumex::transport::is_same_ip (channel.endpoint ().address (), endpoint.address ()))
		{
			// Only counsider channels with the same node id as duplicates if they come from the same IP
			if (channel.node_id () == node_id)
			{
				return true;
			}
		}
		return false;
	});

	if (has_duplicate)
	{
		node.stats.inc (lumex::stat::type::tcp_channels_rejected, lumex::stat::detail::channel_duplicate);
		node.logger.debug (lumex::log::type::tcp_channels, "Rejected duplicate channel: {} ({})", endpoint, lumex::log::as_node_id (node_id));

		return false; // Reject
	}

	return true; // OK
}

std::shared_ptr<lumex::transport::tcp_channel> lumex::transport::tcp_channels::create (const std::shared_ptr<lumex::transport::tcp_socket> & socket, const std::shared_ptr<lumex::transport::tcp_server> & server, const lumex::account & node_id, lumex::node_capabilities_flags flags)
{
	auto const endpoint = socket->get_remote_endpoint ();
	debug_assert (endpoint.address ().is_v6 ());

	lumex::unique_lock<lumex::mutex> lock{ mutex };

	if (stopped)
	{
		return nullptr;
	}

	if (!check (endpoint, node_id))
	{
		node.stats.inc (lumex::stat::type::tcp_channels, lumex::stat::detail::channel_rejected);
		node.logger.debug (lumex::log::type::tcp_channels, "Rejected channel: {} ({})", endpoint, lumex::log::as_node_id (node_id));
		// Rejection reason should be logged earlier

		return nullptr;
	}

	node.stats.inc (lumex::stat::type::tcp_channels, lumex::stat::detail::channel_accepted);
	node.logger.debug (lumex::log::type::tcp_channels, "Accepted channel: {} ({}) ({})",
	socket->get_remote_endpoint (),
	socket->get_endpoint_type (),
	lumex::log::as_node_id (node_id));

	// This should be the only place in node where channels are created
	auto channel = std::make_shared<lumex::transport::tcp_channel> (node, socket);
	channel->set_node_id (node_id);
	channel->set_flags (flags);

	attempts.get<endpoint_tag> ().erase (endpoint);

	auto [_, inserted] = channels.get<endpoint_tag> ().emplace (channel, socket, server);
	debug_assert (inserted);

	lock.unlock ();

	node.observers.channel_connected.notify (channel);

	return channel;
}

void lumex::transport::tcp_channels::erase (lumex::tcp_endpoint const & endpoint_a)
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	channels.get<endpoint_tag> ().erase (endpoint_a);
}

std::size_t lumex::transport::tcp_channels::size () const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	return channels.size ();
}

std::shared_ptr<lumex::transport::tcp_channel> lumex::transport::tcp_channels::find_channel (lumex::tcp_endpoint const & endpoint_a) const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	std::shared_ptr<lumex::transport::tcp_channel> result;
	auto existing (channels.get<endpoint_tag> ().find (endpoint_a));
	if (existing != channels.get<endpoint_tag> ().end ())
	{
		result = existing->channel;
	}
	return result;
}

std::unordered_set<std::shared_ptr<lumex::transport::channel>> lumex::transport::tcp_channels::random_set (std::size_t count_a, uint8_t min_version) const
{
	std::unordered_set<std::shared_ptr<lumex::transport::channel>> result;
	result.reserve (count_a);
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	// Stop trying to fill result with random samples after this many attempts
	auto random_cutoff (count_a * 2);
	// Usually count_a will be much smaller than peers.size()
	// Otherwise make sure we have a cutoff on attempting to randomly fill
	if (!channels.empty ())
	{
		for (auto i (0); i < random_cutoff && result.size () < count_a; ++i)
		{
			auto index = rng.random (channels.size ());
			auto channel = channels.get<random_access_tag> ()[index].channel;
			if (!channel->alive ())
			{
				continue;
			}

			if (channel->get_network_version () >= min_version)
			{
				result.insert (channel);
			}
		}
	}
	return result;
}

void lumex::transport::tcp_channels::random_fill (std::array<lumex::endpoint, 8> & target_a) const
{
	auto peers (random_set (target_a.size ()));
	debug_assert (peers.size () <= target_a.size ());
	auto endpoint (lumex::endpoint (boost::asio::ip::address_v6{}, 0));
	debug_assert (endpoint.address ().is_v6 ());
	std::fill (target_a.begin (), target_a.end (), endpoint);
	auto j (target_a.begin ());
	for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i, ++j)
	{
		debug_assert ((*i)->get_remote_endpoint ().address ().is_v6 ());
		debug_assert (j < target_a.end ());
		*j = (*i)->get_remote_endpoint ();
	}
}

std::shared_ptr<lumex::transport::tcp_channel> lumex::transport::tcp_channels::find_node_id (lumex::account const & node_id_a)
{
	std::shared_ptr<lumex::transport::tcp_channel> result;
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	auto existing (channels.get<node_id_tag> ().find (node_id_a));
	if (existing != channels.get<node_id_tag> ().end ())
	{
		result = existing->channel;
	}
	return result;
}

lumex::tcp_endpoint lumex::transport::tcp_channels::bootstrap_peer ()
{
	lumex::tcp_endpoint result (boost::asio::ip::address_v6::any (), 0);
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	for (auto i (channels.get<last_bootstrap_attempt_tag> ().begin ()), n (channels.get<last_bootstrap_attempt_tag> ().end ()); i != n;)
	{
		if (i->channel->get_network_version () >= node.network_params.network.protocol_version_min)
		{
			result = lumex::transport::map_endpoint_to_tcp (i->channel->get_peering_endpoint ());
			channels.get<last_bootstrap_attempt_tag> ().modify (i, [] (channel_entry & wrapper_a) {
				wrapper_a.channel->set_last_bootstrap_attempt (std::chrono::steady_clock::now ());
			});
			i = n;
		}
		else
		{
			++i;
		}
	}
	return result;
}

bool lumex::transport::tcp_channels::max_ip_connections (lumex::tcp_endpoint const & endpoint_a)
{
	if (node.flags.disable_max_peers_per_ip)
	{
		return false;
	}
	bool result{ false };
	auto const address (lumex::transport::ipv4_address_or_ipv6_subnet (endpoint_a.address ()));
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	result = channels.get<ip_address_tag> ().count (address) >= node.config.network->max_peers_per_ip;
	if (!result)
	{
		result = attempts.get<ip_address_tag> ().count (address) >= node.config.network->max_peers_per_ip;
	}
	if (result)
	{
		node.stats.inc (lumex::stat::type::tcp, lumex::stat::detail::max_per_ip, lumex::stat::dir::out);
	}
	return result;
}

bool lumex::transport::tcp_channels::max_subnetwork_connections (lumex::tcp_endpoint const & endpoint_a)
{
	if (node.flags.disable_max_peers_per_subnetwork)
	{
		return false;
	}
	bool result{ false };
	auto const subnet (lumex::transport::map_address_to_subnetwork (endpoint_a.address ()));
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	result = channels.get<subnetwork_tag> ().count (subnet) >= node.config.network->max_peers_per_subnetwork;
	if (!result)
	{
		result = attempts.get<subnetwork_tag> ().count (subnet) >= node.config.network->max_peers_per_subnetwork;
	}
	if (result)
	{
		node.stats.inc (lumex::stat::type::tcp, lumex::stat::detail::max_per_subnetwork, lumex::stat::dir::out);
	}
	return result;
}

bool lumex::transport::tcp_channels::max_ip_or_subnetwork_connections (lumex::tcp_endpoint const & endpoint_a)
{
	return max_ip_connections (endpoint_a) || max_subnetwork_connections (endpoint_a);
}

bool lumex::transport::tcp_channels::track_reachout (lumex::endpoint const & endpoint_a)
{
	auto const tcp_endpoint = lumex::transport::map_endpoint_to_tcp (endpoint_a);

	// Don't overload single IP
	if (max_ip_or_subnetwork_connections (tcp_endpoint))
	{
		return false;
	}
	if (node.network.excluded_peers.check (tcp_endpoint))
	{
		return false;
	}
	if (node.flags.disable_tcp_realtime)
	{
		return false;
	}

	// Don't keepalive to nodes that already sent us something
	if (find_channel (tcp_endpoint) != nullptr)
	{
		return false;
	}

	lumex::lock_guard<lumex::mutex> lock{ mutex };
	auto [it, inserted] = attempts.emplace (tcp_endpoint);
	return inserted;
}

void lumex::transport::tcp_channels::purge (std::chrono::steady_clock::time_point cutoff_deadline)
{
	auto channels_l = all_channels ();

	auto should_close = [this, cutoff_deadline] (auto const & channel) {
		// Remove channels that haven't successfully sent a message within the cutoff time
		if (auto last = channel->get_last_packet_sent (); last < cutoff_deadline)
		{
			node.stats.inc (lumex::stat::type::tcp_channels_purge, lumex::stat::detail::idle);
			node.logger.debug (lumex::log::type::tcp_channels, "Closing idle channel: {} (idle for {}s)",
			channel,
			lumex::log::seconds_delta (last));

			return true; // Close
		}
		// Check if any tcp channels belonging to old protocol versions which may still be alive due to async operations
		if (channel->get_network_version () < node.network_params.network.protocol_version_min)
		{
			node.stats.inc (lumex::stat::type::tcp_channels_purge, lumex::stat::detail::outdated);
			node.logger.debug (lumex::log::type::tcp_channels, "Closing channel with old protocol version: {}", channel);

			return true; // Close
		}
		return false;
	};

	// Close stale channels without holding the mutex
	for (auto const & channel : channels_l)
	{
		if (should_close (channel))
		{
			channel->close ();
		}
	}

	lumex::unique_lock<lumex::mutex> lock{ mutex };

	// Remove keepalive attempt tracking for attempts older than cutoff
	auto attempts_cutoff (attempts.get<last_attempt_tag> ().lower_bound (cutoff_deadline));
	attempts.get<last_attempt_tag> ().erase (attempts.get<last_attempt_tag> ().begin (), attempts_cutoff);

	// Erase dead channels from list, but close them outside of the lock
	auto erased_connections = erase_if_and_collect (channels, [this] (auto const & entry) {
		return !entry.channel->alive ();
	});

	lock.unlock ();

	for (auto const & connection : erased_connections)
	{
		node.stats.inc (lumex::stat::type::tcp_channels, lumex::stat::detail::erase_dead);
		node.logger.debug (lumex::log::type::tcp_channels, "Removing dead channel: {}", connection.channel);

		connection.channel->close ();
	}
}

void lumex::transport::tcp_channels::keepalive ()
{
	lumex::messages::keepalive message{ node.network_params.network };
	node.network.random_fill (message.peers);

	lumex::unique_lock<lumex::mutex> lock{ mutex };

	auto const cutoff_time = std::chrono::steady_clock::now () - node.network_params.network.keepalive_period;

	// Wake up channels
	std::vector<std::shared_ptr<lumex::transport::tcp_channel>> to_wakeup;
	for (auto const & entry : channels)
	{
		if (entry.channel->get_last_packet_sent () < cutoff_time)
		{
			to_wakeup.push_back (entry.channel);
		}
	}

	lock.unlock ();

	for (auto & channel : to_wakeup)
	{
		channel->send (message, lumex::transport::traffic_type::keepalive);
	}
}

std::optional<lumex::messages::keepalive> lumex::transport::tcp_channels::sample_keepalive ()
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };

	size_t counter = 0;
	while (counter++ < channels.size ())
	{
		auto index = rng.random (channels.size ());
		if (auto channel = channels.get<random_access_tag> ()[index].channel)
		{
			if (auto keepalive = channel->pop_last_keepalive ())
			{
				return keepalive;
			}
		}
	}

	return std::nullopt;
}

std::deque<std::shared_ptr<lumex::transport::channel>> lumex::transport::tcp_channels::list (uint8_t minimum_version) const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };

	std::deque<std::shared_ptr<lumex::transport::channel>> result;
	for (auto const & entry : channels)
	{
		if (entry.channel->get_network_version () >= minimum_version)
		{
			result.push_back (entry.channel);
		}
	}
	return result;
}

std::deque<std::shared_ptr<lumex::transport::channel>> lumex::transport::tcp_channels::list (channel_filter filter) const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };

	std::deque<std::shared_ptr<lumex::transport::channel>> result;
	for (auto const & entry : channels)
	{
		if (filter == nullptr || filter (entry.channel))
		{
			result.push_back (entry.channel);
		}
	}
	return result;
}

bool lumex::transport::tcp_channels::start_tcp (lumex::endpoint const & endpoint)
{
	return node.tcp_listener.connect (endpoint.address (), endpoint.port ());
}

auto lumex::transport::tcp_channels::all_sockets () const -> std::deque<std::shared_ptr<tcp_socket>>
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	auto r = channels | std::views::transform ([] (auto const & entry) { return entry.socket; });
	return { r.begin (), r.end () };
}

auto lumex::transport::tcp_channels::all_servers () const -> std::deque<std::shared_ptr<tcp_server>>
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	auto r = channels | std::views::transform ([] (auto const & entry) { return entry.server; });
	return { r.begin (), r.end () };
}

auto lumex::transport::tcp_channels::all_channels () const -> std::deque<std::shared_ptr<tcp_channel>>
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	auto r = channels | std::views::transform ([] (auto const & entry) { return entry.channel; });
	return { r.begin (), r.end () };
}

lumex::container_info lumex::transport::tcp_channels::container_info () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	lumex::container_info info;
	info.put ("channels", channels.size ());
	info.put ("attempts", attempts.size ());
	return info;
}