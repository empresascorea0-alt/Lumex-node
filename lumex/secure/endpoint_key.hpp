#pragma once

#include <lumex/lib/network_types.hpp>

#include <array>
#include <cstdint>

namespace lumex
{
class endpoint_key final
{
public:
	endpoint_key () = default;
	endpoint_key (lumex::endpoint const &);

	/*
	 * @param address_a This should be in network byte order
	 * @param port_a This should be in host byte order
	 */
	endpoint_key (std::array<uint8_t, 16> const & address_a, uint16_t port_a);

	/*
	 * @return The ipv6 address in network byte order
	 */
	std::array<uint8_t, 16> const & address_bytes () const;

	/*
	 * @return The port in host byte order
	 */
	uint16_t port () const;

	lumex::endpoint endpoint () const;

private:
	// Both stored internally in network byte order
	std::array<uint8_t, 16> address;
	uint16_t network_port{ 0 };
};
}
