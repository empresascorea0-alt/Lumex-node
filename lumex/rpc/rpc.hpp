#pragma once

#include <lumex/boost/asio/ip/tcp.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/rpc_handler_interface.hpp>
#include <lumex/lib/rpcconfig.hpp>

namespace boost
{
namespace asio
{
	class io_context;
}
}

namespace lumex
{
class rpc_handler_interface;

class rpc : public std::enable_shared_from_this<rpc>
{
public:
	rpc (std::shared_ptr<boost::asio::io_context>, lumex::rpc_config config_a, lumex::rpc_handler_interface & rpc_handler_interface_a);
	virtual ~rpc ();

	void start ();
	void stop ();

	virtual void accept ();

	std::uint16_t listening_port () const
	{
		return acceptor.local_endpoint ().port ();
	}

public:
	lumex::logger logger{ "rpc" };
	lumex::rpc_config config;
	std::shared_ptr<boost::asio::io_context> io_ctx_shared;
	boost::asio::io_context & io_ctx;
	boost::asio::ip::tcp::acceptor acceptor;
	lumex::rpc_handler_interface & rpc_handler_interface;
	bool stopped{ false };
};

/** Returns the correct RPC implementation based on TLS configuration */
std::shared_ptr<lumex::rpc> get_rpc (std::shared_ptr<boost::asio::io_context>, lumex::rpc_config const & config_a, lumex::rpc_handler_interface & rpc_handler_interface_a);
}
