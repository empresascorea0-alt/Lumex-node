#include <lumex/boost/asio/post.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/transport/loopback.hpp>
#include <lumex/node/transport/message_deserializer.hpp>
#include <lumex/secure/network_params.hpp>

#include <boost/format.hpp>

lumex::transport::loopback_channel::loopback_channel (lumex::node & node) :
	transport::channel{ node },
	endpoint{ node.network.endpoint () }
{
	set_node_id (node.get_node_id ());
	set_network_version (node.network_params.network.protocol_version);
}

bool lumex::transport::loopback_channel::send_impl (lumex::messages::message const & message, lumex::transport::traffic_type traffic_type, lumex::transport::channel::callback_t callback)
{
	node.stats.inc (lumex::stat::type::message_loopback, to_stat_detail (message.type ()), lumex::stat::dir::in);

	node.inbound (message, shared_from_this ());

	if (callback)
	{
		boost::asio::post (node.io_ctx, [callback_l = std::move (callback)] () {
			callback_l (boost::system::errc::make_error_code (boost::system::errc::success), 0);
		});
	}

	return true;
}

std::string lumex::transport::loopback_channel::to_string () const
{
	return boost::str (boost::format ("%1%") % endpoint);
}
