#include <lumex/boost/asio/post.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/transport/fake.hpp>
#include <lumex/secure/network_params.hpp>

#include <boost/format.hpp>

lumex::transport::fake::channel::channel (lumex::node & node) :
	transport::channel{ node },
	endpoint{ node.network.endpoint () }
{
	set_node_id (node.get_node_id ());
	set_network_version (node.network_params.network.protocol_version);
}

/**
 * The send function behaves like a null device, it throws the data away and returns success.
 */
bool lumex::transport::fake::channel::send_impl (lumex::messages::message const & message, lumex::transport::traffic_type traffic_type, lumex::transport::channel::callback_t callback)
{
	auto buffer = message.to_shared_const_buffer ();
	auto size = buffer.size ();
	if (callback)
	{
		boost::asio::post (node.io_ctx, [callback, size] () {
			callback (boost::system::errc::make_error_code (boost::system::errc::success), size);
		});
	}
	return true;
}

std::string lumex::transport::fake::channel::to_string () const
{
	return boost::str (boost::format ("%1%") % endpoint);
}
