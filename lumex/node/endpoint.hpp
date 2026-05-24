#pragma once

#include <lumex/lib/network_types.hpp>
#include <lumex/secure/common.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace boost::asio::ip
{
class address;
}

namespace lumex
{
bool parse_port (std::string const &, uint16_t &);
bool parse_address (std::string const &, boost::asio::ip::address &);
bool parse_address_port (std::string const &, boost::asio::ip::address &, uint16_t &);
bool parse_endpoint (std::string const &, lumex::endpoint &);
std::optional<lumex::endpoint> parse_endpoint (std::string const &);
bool parse_tcp_endpoint (std::string const &, lumex::tcp_endpoint &);
uint64_t ip_address_hash_raw (boost::asio::ip::address const & ip_a, uint16_t port = 0);
uint64_t endpoint_hash_raw (lumex::endpoint const & endpoint_a);
}

namespace
{
template <std::size_t size>
struct endpoint_hash
{
};

template <>
struct endpoint_hash<8>
{
	std::size_t operator() (lumex::endpoint const & endpoint_a) const
	{
		return lumex::endpoint_hash_raw (endpoint_a);
	}
};

template <>
struct endpoint_hash<4>
{
	std::size_t operator() (lumex::endpoint const & endpoint_a) const
	{
		uint64_t big = lumex::endpoint_hash_raw (endpoint_a);
		uint32_t result (static_cast<uint32_t> (big) ^ static_cast<uint32_t> (big >> 32));
		return result;
	}
};

template <std::size_t size>
struct ip_address_hash
{
};

template <>
struct ip_address_hash<8>
{
	std::size_t operator() (boost::asio::ip::address const & ip_address_a) const
	{
		return lumex::ip_address_hash_raw (ip_address_a);
	}
};

template <>
struct ip_address_hash<4>
{
	std::size_t operator() (boost::asio::ip::address const & ip_address_a) const
	{
		uint64_t big (lumex::ip_address_hash_raw (ip_address_a));
		uint32_t result (static_cast<uint32_t> (big) ^ static_cast<uint32_t> (big >> 32));
		return result;
	}
};
}

namespace std
{
template <>
struct hash<::lumex::endpoint>;

#ifndef BOOST_ASIO_HAS_STD_HASH
template <>
struct hash<boost::asio::ip::address>;
#endif
}

namespace boost
{
template <>
struct hash<::lumex::endpoint>;

template <>
struct hash<boost::asio::ip::address>;
}
