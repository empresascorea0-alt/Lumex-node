#include <lumex/lib/object_stream.hpp>
#include <lumex/messages/messages.hpp>
#include <lumex/node/endpoint.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/transport/channel.hpp>
#include <lumex/node/transport/transport.hpp>
#include <lumex/secure/network_params.hpp>

lumex::transport::channel::channel (lumex::node & node_a) :
	node{ node_a }
{
	set_network_version (node_a.network_params.network.protocol_version);
}

bool lumex::transport::channel::send (lumex::messages::message const & message, lumex::transport::traffic_type traffic_type, callback_t callback)
{
	bool sent = send_impl (message, traffic_type, std::move (callback));
	node.stats.inc (sent ? lumex::stat::type::message : lumex::stat::type::message_drop, to_stat_detail (message.type ()), lumex::stat::dir::out, /* aggregate all */ true);
	return sent;
}

void lumex::transport::channel::set_peering_endpoint (lumex::endpoint endpoint)
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	peering_endpoint = endpoint;
}

lumex::endpoint lumex::transport::channel::get_peering_endpoint () const
{
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		if (peering_endpoint)
		{
			return *peering_endpoint;
		}
	}
	return get_remote_endpoint ();
}

void lumex::transport::channel::set_last_keepalive (lumex::messages::keepalive const & message)
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	last_keepalive = message;
}

std::optional<lumex::messages::keepalive> lumex::transport::channel::pop_last_keepalive ()
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	auto result = last_keepalive;
	last_keepalive.reset ();
	return result;
}

std::shared_ptr<lumex::node> lumex::transport::channel::owner () const
{
	return node.shared ();
}

void lumex::transport::channel::operator() (lumex::object_stream & obs) const
{
	obs.write ("remote_endpoint", get_remote_endpoint ());
	obs.write ("local_endpoint", get_local_endpoint ());
	obs.write ("peering_endpoint", get_peering_endpoint ());
	obs.write ("node_id", get_node_id ().to_node_id ());
}
