#pragma once

#include <lumex/lib/locks.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/node/bandwidth_limiter.hpp>
#include <lumex/node/endpoint.hpp>
#include <lumex/node/transport/tcp_socket.hpp>

#include <boost/asio/ip/network_v6.hpp>

namespace lumex::transport
{
lumex::endpoint map_endpoint_to_v6 (lumex::endpoint const &);
lumex::endpoint map_tcp_to_endpoint (lumex::tcp_endpoint const &);
lumex::tcp_endpoint map_endpoint_to_tcp (lumex::endpoint const &);
boost::asio::ip::address map_address_to_subnetwork (boost::asio::ip::address);
boost::asio::ip::address ipv4_address_or_ipv6_subnet (boost::asio::ip::address);
boost::asio::ip::address_v6 mapped_from_v4_bytes (unsigned long);
boost::asio::ip::address_v6 mapped_from_v4_or_v6 (boost::asio::ip::address const &);
bool is_ipv4_or_v4_mapped_address (boost::asio::ip::address const &);
bool is_same_ip (boost::asio::ip::address const &, boost::asio::ip::address const &);
bool is_same_subnetwork (boost::asio::ip::address const &, boost::asio::ip::address const &);

// Unassigned, reserved, self
bool reserved_address (lumex::endpoint const &, bool allow_local_peers = false);

using address_socket_mmap = std::multimap<boost::asio::ip::address, std::weak_ptr<tcp_socket>>;

namespace socket_functions
{
	boost::asio::ip::network_v6 get_ipv6_subnet_address (boost::asio::ip::address_v6 const &, std::size_t);
	boost::asio::ip::address first_ipv6_subnet_address (boost::asio::ip::address_v6 const &, std::size_t);
	boost::asio::ip::address last_ipv6_subnet_address (boost::asio::ip::address_v6 const &, std::size_t);
	std::size_t count_subnetwork_connections (lumex::transport::address_socket_mmap const &, boost::asio::ip::address_v6 const &, std::size_t);
}

void throw_if_error (boost::system::error_code const & ec);
}

namespace lumex
{
lumex::stat::detail to_stat_detail (boost::system::error_code const &);
}