#include <lumex/lib/object_stream.hpp>
#include <lumex/lib/stream.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/messages/keepalive.hpp>
#include <lumex/messages/message_visitor.hpp>

#include <boost/asio/ip/address_v6.hpp>

namespace lumex::messages
{
keepalive::keepalive (lumex::network_constants const & constants) :
	message (constants, message_type::keepalive)
{
	lumex::endpoint endpoint (boost::asio::ip::address_v6{}, 0);
	for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i)
	{
		*i = endpoint;
	}
}

keepalive::keepalive (bool & error_a, lumex::stream & stream_a, message_header const & header_a) :
	message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void keepalive::visit (message_visitor & visitor_a) const
{
	visitor_a.keepalive (*this);
}

void keepalive::serialize (lumex::stream & stream_a) const
{
	header.serialize (stream_a);
	for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
	{
		debug_assert (i->address ().is_v6 ());
		auto bytes (i->address ().to_v6 ().to_bytes ());
		write (stream_a, bytes);
		write (stream_a, i->port ());
	}
}

bool keepalive::deserialize (lumex::stream & stream_a)
{
	debug_assert (header.type == message_type::keepalive);
	auto error (false);
	for (auto i (peers.begin ()), j (peers.end ()); i != j && !error; ++i)
	{
		std::array<uint8_t, 16> address;
		uint16_t port;
		if (!try_read (stream_a, address) && !try_read (stream_a, port))
		{
			*i = lumex::endpoint (boost::asio::ip::address_v6 (address), port);
		}
		else
		{
			error = true;
		}
	}
	return error;
}

bool keepalive::operator== (keepalive const & other_a) const
{
	return peers == other_a.peers;
}

void keepalive::operator() (lumex::object_stream & obs) const
{
	message::operator() (obs); // Write common data

	obs.write_range ("peers", peers);
}
}
