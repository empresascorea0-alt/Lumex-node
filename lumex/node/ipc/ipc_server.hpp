#pragma once

#include <lumex/lib/errors.hpp>
#include <lumex/lib/ipc.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/node/ipc/ipc_access_config.hpp>
#include <lumex/node/ipc/ipc_broker.hpp>
#include <lumex/node/node_rpc_config.hpp>

#include <boost/asio/signal_set.hpp>

#include <atomic>
#include <memory>

namespace lumex
{
class node;
class error;
namespace ipc
{
	class access;
	/** The IPC server accepts connections on one or more configured transports */
	class ipc_server final : public std::enable_shared_from_this<ipc_server>
	{
	public:
		ipc_server (lumex::node & node, lumex::node_rpc_config const & node_rpc_config);
		~ipc_server ();
		void stop ();

		std::optional<std::uint16_t> listening_tcp_port () const;

		lumex::node & node;
		lumex::node_rpc_config const & node_rpc_config;

		/** Unique counter/id shared across sessions */
		std::atomic<uint64_t> id_dispenser{ 1 };
		std::shared_ptr<lumex::ipc::broker> get_broker ();
		lumex::ipc::access & get_access ();
		lumex::error reload_access_config ();

		lumex::logger logger{ "ipc_server" };

	private:
		void setup_callbacks ();

		std::shared_ptr<lumex::ipc::broker> broker;
		lumex::ipc::access access;
		std::unique_ptr<dsock_file_remover> file_remover;
		std::vector<std::shared_ptr<lumex::ipc::transport>> transports;
		std::shared_ptr<boost::asio::signal_set> signals;
	};
}
}
