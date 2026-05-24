#pragma once

#include <lumex/lib/stream.hpp>
#include <lumex/messages/messages.hpp>
#include <lumex/node/endpoint.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/node/transport/fwd.hpp>
#include <lumex/node/transport/tcp_socket.hpp>

#include <atomic>

namespace lumex::transport
{
class tcp_server final : public std::enable_shared_from_this<tcp_server>
{
public:
	tcp_server (lumex::node &, std::shared_ptr<lumex::transport::tcp_socket>);
	~tcp_server ();

	void start ();

	void close ();
	void close_async (); // Safe to call from io context

	bool alive () const;

public:
	lumex::endpoint get_remote_endpoint () const
	{
		return socket->get_remote_endpoint ();
	}
	lumex::endpoint get_local_endpoint () const
	{
		return socket->get_local_endpoint ();
	}
	lumex::transport::socket_type get_type () const
	{
		return socket->type ();
	}

private:
	enum class handshake_status
	{
		abort,
		handshake,
		realtime,
		bootstrap,
	};

	void stop ();

	asio::awaitable<void> start_impl ();
	asio::awaitable<handshake_status> perform_handshake ();
	asio::awaitable<void> run_realtime ();
	asio::awaitable<lumex::deserialize_message_result> receive_message ();
	asio::awaitable<lumex::deserialize_message_result> receive_message_impl ();
	asio::awaitable<lumex::buffer_view> read_socket (size_t size) const;

	asio::awaitable<handshake_status> process_handshake (lumex::messages::node_id_handshake const & message);
	asio::awaitable<void> send_handshake_response (lumex::messages::node_id_handshake::query_payload const & query, lumex::messages::handshake_version version);
	asio::awaitable<void> send_handshake_request ();

private:
	lumex::node & node;

	std::shared_ptr<lumex::transport::tcp_socket> socket;
	std::shared_ptr<lumex::transport::tcp_channel> channel; // Every realtime connection must have an associated channel

	lumex::async::strand strand;
	lumex::async::task task;

	lumex::shared_buffer buffer;
	static size_t constexpr max_buffer_size = 64 * 1024; // 64 KB

	std::atomic<bool> handshake_received{ false };

private:
	bool to_bootstrap_connection ();
	bool to_realtime_connection (lumex::account const & node_id, lumex::node_capabilities_flags flags);

private: // Visitors
	class realtime_message_visitor : public lumex::messages::message_visitor
	{
	public:
		bool process{ false };

		void keepalive (lumex::messages::keepalive const &) override;
		void publish (lumex::messages::publish const &) override;
		void confirm_req (lumex::messages::confirm_req const &) override;
		void confirm_ack (lumex::messages::confirm_ack const &) override;
		void frontier_req (lumex::messages::frontier_req const &) override;
		void telemetry_req (lumex::messages::telemetry_req const &) override;
		void telemetry_ack (lumex::messages::telemetry_ack const &) override;
		void asc_pull_req (lumex::messages::asc_pull_req const &) override;
		void asc_pull_ack (lumex::messages::asc_pull_ack const &) override;
	};
};
}
