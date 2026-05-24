#include <lumex/secure/endpoint_key.hpp>

#include <boost/endian/conversion.hpp>

lumex::endpoint_key::endpoint_key (std::array<uint8_t, 16> const & address_a, uint16_t port_a) :
	address (address_a),
	network_port (boost::endian::native_to_big (port_a))
{
}

lumex::endpoint_key::endpoint_key (lumex::endpoint const & endpoint_a) :
	endpoint_key (endpoint_a.address ().to_v6 ().to_bytes (), endpoint_a.port ())
{
}

std::array<uint8_t, 16> const & lumex::endpoint_key::address_bytes () const
{
	return address;
}

uint16_t lumex::endpoint_key::port () const
{
	return boost::endian::big_to_native (network_port);
}

lumex::endpoint lumex::endpoint_key::endpoint () const
{
	return { boost::asio::ip::address_v6 (address), port () };
}
