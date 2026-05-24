#pragma once

#include <lumex/lib/ipc_client.hpp>
#include <lumex/lib/locks.hpp>
#include <lumex/lib/rpc_handler_interface.hpp>
#include <lumex/lib/rpcconfig.hpp>
#include <lumex/rpc/rpc.hpp>

#include <atomic>
#include <deque>

namespace lumex
{
struct ipc_connection
{
	ipc_connection (lumex::ipc::ipc_client && client_a, bool is_available_a) :
		client (std::move (client_a)), is_available (is_available_a)
	{
	}

	lumex::ipc::ipc_client client;
	std::atomic<bool> is_available{ false };
};

struct rpc_request
{
	rpc_request (std::string const & action_a, std::string const & body_a, std::function<void (std::string const &)> response_a) :
		action (action_a), body (body_a), response (response_a)
	{
	}

	rpc_request (int rpc_api_version_a, std::string const & body_a, std::function<void (std::string const &)> response_a) :
		rpc_api_version (rpc_api_version_a), body (body_a), response (response_a)
	{
	}

	rpc_request (int rpc_api_version_a, std::string const & action_a, std::string const & body_a, std::function<void (std::string const &)> response_a) :
		rpc_api_version (rpc_api_version_a), action (action_a), body (body_a), response (response_a)
	{
	}

	int rpc_api_version{ 1 };
	std::string action;
	std::string body;
	std::function<void (std::string const &)> response;
};

class rpc_request_processor
{
public:
	rpc_request_processor (std::shared_ptr<boost::asio::io_context> io_ctx, lumex::rpc_config & rpc_config);
	rpc_request_processor (std::shared_ptr<boost::asio::io_context> io_ctx, lumex::rpc_config & rpc_config, std::uint16_t ipc_port_a);
	~rpc_request_processor ();
	void stop ();
	void add (std::shared_ptr<rpc_request> const & request);
	std::function<void ()> stop_callback;

private:
	void run ();
	void read_payload (std::shared_ptr<lumex::ipc_connection> const & connection, std::shared_ptr<std::vector<uint8_t>> const & res, std::shared_ptr<lumex::rpc_request> const & rpc_request);
	void try_reconnect_and_execute_request (std::shared_ptr<lumex::ipc_connection> const & connection, lumex::shared_const_buffer const & req, std::shared_ptr<std::vector<uint8_t>> const & res, std::shared_ptr<lumex::rpc_request> const & rpc_request);
	void make_available (lumex::ipc_connection & connection);

	std::vector<std::shared_ptr<lumex::ipc_connection>> connections;
	lumex::mutex request_mutex;
	lumex::mutex connections_mutex;
	bool stopped{ false };
	std::deque<std::shared_ptr<lumex::rpc_request>> requests;
	lumex::condition_variable condition;
	std::string const ipc_address;
	uint16_t const ipc_port;
	std::thread thread;
};

class ipc_rpc_processor final : public lumex::rpc_handler_interface
{
public:
	ipc_rpc_processor (std::shared_ptr<boost::asio::io_context> io_ctx, lumex::rpc_config & rpc_config) :
		rpc_request_processor (std::move (io_ctx), rpc_config)
	{
	}
	ipc_rpc_processor (std::shared_ptr<boost::asio::io_context> io_ctx, lumex::rpc_config & rpc_config, std::uint16_t ipc_port_a) :
		rpc_request_processor (std::move (io_ctx), rpc_config, ipc_port_a)
	{
	}

	void process_request (std::string const & action_a, std::string const & body_a, std::function<void (std::string const &)> response_a) override
	{
		rpc_request_processor.add (std::make_shared<lumex::rpc_request> (action_a, body_a, response_a));
	}

	void process_request_v2 (rpc_handler_request_params const & params_a, std::string const & body_a, std::function<void (std::shared_ptr<std::string> const &)> response_a) override
	{
		std::string body_l = params_a.json_envelope (body_a);
		rpc_request_processor.add (std::make_shared<lumex::rpc_request> (2 /* rpc version */, body_l, [response_a] (std::string const & resp) {
			auto resp_l (std::make_shared<std::string> (resp));
			response_a (resp_l);
		}));
	}

	void stop () override
	{
		rpc_request_processor.stop ();
	}

	void rpc_instance (lumex::rpc & rpc) override
	{
		rpc_request_processor.stop_callback = [&rpc] () {
			rpc.stop ();
		};
	}

private:
	lumex::rpc_request_processor rpc_request_processor;
};
}
