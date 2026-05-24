#include <lumex/crypto/blake2/blake2.h>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/memory.hpp>
#include <lumex/lib/stream.hpp>
#include <lumex/lib/vote.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/election.hpp>
#include <lumex/node/endpoint.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/wallet.hpp>
#include <lumex/secure/network_params.hpp>

#include <boost/format.hpp>

uint64_t lumex::ip_address_hash_raw (boost::asio::ip::address const & ip_a, uint16_t port)
{
	debug_assert (ip_a.is_v6 ());
	uint64_t result;
	lumex::uint128_union address;
	address.bytes = ip_a.to_v6 ().to_bytes ();
	blake2b_state state;
	blake2b_init (&state, sizeof (result));
	blake2b_update (&state, lumex::hardened_constants::get ().random_128.bytes.data (), lumex::hardened_constants::get ().random_128.bytes.size ());
	if (port != 0)
	{
		blake2b_update (&state, &port, sizeof (port));
	}
	blake2b_update (&state, address.bytes.data (), address.bytes.size ());
	blake2b_final (&state, &result, sizeof (result));
	return result;
}

uint64_t lumex::endpoint_hash_raw (lumex::endpoint const & endpoint_a)
{
	uint64_t result (lumex::ip_address_hash_raw (endpoint_a.address (), endpoint_a.port ()));
	return result;
}

bool lumex::parse_port (std::string const & string_a, uint16_t & port_a)
{
	bool result = false;
	try
	{
		port_a = boost::lexical_cast<uint16_t> (string_a);
	}
	catch (...)
	{
		result = true;
	}
	return result;
}

// Can handle both ipv4 & ipv6 addresses (with and without square brackets)
bool lumex::parse_address (std::string const & address_text_a, boost::asio::ip::address & address_a)
{
	auto address_text = address_text_a;
	if (!address_text.empty () && address_text.front () == '[' && address_text.back () == ']')
	{
		// Chop the square brackets off as make_address doesn't always like them
		address_text = address_text.substr (1, address_text.size () - 2);
	}

	boost::system::error_code address_ec;
	address_a = boost::asio::ip::make_address (address_text, address_ec);
	return !!address_ec;
}

bool lumex::parse_address_port (std::string const & string, boost::asio::ip::address & address_a, uint16_t & port_a)
{
	auto result (false);
	auto port_position (string.rfind (':'));
	if (port_position != std::string::npos && port_position > 0)
	{
		std::string port_string (string.substr (port_position + 1));
		try
		{
			uint16_t port;
			result = parse_port (port_string, port);
			if (!result)
			{
				boost::system::error_code ec;
				auto address (boost::asio::ip::make_address_v6 (string.substr (0, port_position), ec));
				if (!ec)
				{
					address_a = address;
					port_a = port;
				}
				else
				{
					result = true;
				}
			}
			else
			{
				result = true;
			}
		}
		catch (...)
		{
			result = true;
		}
	}
	else
	{
		result = true;
	}
	return result;
}

bool lumex::parse_endpoint (std::string const & string, lumex::endpoint & endpoint_a)
{
	boost::asio::ip::address address;
	uint16_t port;
	auto result (parse_address_port (string, address, port));
	if (!result)
	{
		endpoint_a = lumex::endpoint (address, port);
	}
	return result;
}

std::optional<lumex::endpoint> lumex::parse_endpoint (const std::string & str)
{
	lumex::endpoint endpoint;
	if (!parse_endpoint (str, endpoint))
	{
		return endpoint; // Success
	}
	return {};
}

bool lumex::parse_tcp_endpoint (std::string const & string, lumex::tcp_endpoint & endpoint_a)
{
	boost::asio::ip::address address;
	uint16_t port;
	auto result (parse_address_port (string, address, port));
	if (!result)
	{
		endpoint_a = lumex::tcp_endpoint (address, port);
	}
	return result;
}

lumex::node_singleton_memory_pool_purge_guard::node_singleton_memory_pool_purge_guard () :
	cleanup_guard ({ lumex::block_memory_pool_purge, lumex::purge_shared_ptr_singleton_pool_memory<lumex::vote>, lumex::purge_shared_ptr_singleton_pool_memory<lumex::election> })
{
}
