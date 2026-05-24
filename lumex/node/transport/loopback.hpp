#pragma once

#include <lumex/node/transport/channel.hpp>
#include <lumex/node/transport/transport.hpp>

namespace lumex::transport
{
class loopback_channel final : public lumex::transport::channel, public std::enable_shared_from_this<loopback_channel>
{
public:
	explicit loopback_channel (lumex::node & node);

	std::string to_string () const override;

	lumex::endpoint get_remote_endpoint () const override
	{
		return endpoint;
	}

	lumex::endpoint get_local_endpoint () const override
	{
		return endpoint;
	}

	lumex::transport::transport_type get_type () const override
	{
		return lumex::transport::transport_type::loopback;
	}

	void close () override
	{
		// Can't be closed
	}

protected:
	bool send_impl (lumex::messages::message const &, lumex::transport::traffic_type, lumex::transport::channel::callback_t) override;

private:
	lumex::endpoint const endpoint;
};
}