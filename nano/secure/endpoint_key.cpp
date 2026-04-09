#include <nano/secure/endpoint_key.hpp>

#include <boost/endian/conversion.hpp>

nano::endpoint_key::endpoint_key (std::array<uint8_t, 16> const & address_a, uint16_t port_a) :
	address (address_a),
	network_port (boost::endian::native_to_big (port_a))
{
}

nano::endpoint_key::endpoint_key (nano::endpoint const & endpoint_a) :
	endpoint_key (endpoint_a.address ().to_v6 ().to_bytes (), endpoint_a.port ())
{
}

std::array<uint8_t, 16> const & nano::endpoint_key::address_bytes () const
{
	return address;
}

uint16_t nano::endpoint_key::port () const
{
	return boost::endian::big_to_native (network_port);
}

nano::endpoint nano::endpoint_key::endpoint () const
{
	return { boost::asio::ip::address_v6 (address), port () };
}
