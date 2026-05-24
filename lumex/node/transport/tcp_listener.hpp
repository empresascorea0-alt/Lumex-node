#pragma once

#include <lumex/lib/async.hpp>
#include <lumex/lib/fwd.hpp>
#include <lumex/lib/observer_set.hpp>
#include <lumex/node/endpoint.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/node/transport/common.hpp>
#include <lumex/node/transport/tcp_config.hpp>

#include <boost/asio.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index_container.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <list>
#include <string_view>
#include <thread>

namespace mi = boost::multi_index;
namespace asio = boost::asio;

namespace lumex::transport
{
class tcp_listener final
{
public:
	enum class connection_type
	{
		inbound,
		outbound,
	};

	enum class accept_result
	{
		invalid,
		accepted,
		rejected,
		rejected_excluded,
		rejected_max_per_ip,
		rejected_max_per_subnetwork,
		rejected_max_inbound,
		rejected_max_outbound,
		error,
	};

public:
	tcp_listener (uint16_t port, tcp_config const &, lumex::node &);
	~tcp_listener ();

	void start ();
	void stop ();

	/**
	 * @param port is optional, if 0 then default peering port is used
	 * @return true if connection attempt was initiated
	 */
	bool connect (asio::ip::address ip, uint16_t port = 0);

	lumex::tcp_endpoint endpoint () const;

	size_t connection_count () const;
	size_t connection_count (connection_type) const;
	size_t attempt_count () const;
	size_t realtime_count () const;
	size_t bootstrap_count () const;

	std::deque<std::shared_ptr<tcp_socket>> all_sockets () const;
	std::deque<std::shared_ptr<tcp_server>> all_servers () const;

	lumex::container_info container_info () const;

public: // Events
	using connection_accepted_event_t = lumex::observer_set<std::shared_ptr<tcp_socket>, std::shared_ptr<tcp_server>>;
	connection_accepted_event_t connection_accepted;

private: // Dependencies
	tcp_config const & config;
	lumex::node & node;
	lumex::stats & stats;
	lumex::logger & logger;

private:
	asio::awaitable<void> start_impl ();
	asio::awaitable<void> run ();
	asio::awaitable<void> wait_available_slots () const;

	void run_cleanup ();
	void purge (lumex::unique_lock<lumex::mutex> &);
	void timeout ();

	asio::awaitable<void> connect_impl (asio::ip::tcp::endpoint);
	asio::awaitable<asio::ip::tcp::socket> connect_socket (asio::ip::tcp::endpoint);

	struct accept_return
	{
		accept_result result;
		std::shared_ptr<lumex::transport::tcp_socket> socket;
		std::shared_ptr<lumex::transport::tcp_server> server;
	};

	accept_return accept_one (asio::ip::tcp::socket, connection_type);
	accept_result check_limits (asio::ip::address const & ip, connection_type) const;
	asio::awaitable<asio::ip::tcp::socket> accept_socket ();

	size_t count_per_type (connection_type) const;
	size_t count_per_ip (asio::ip::address const & ip) const;
	size_t count_per_subnetwork (asio::ip::address const & ip) const;
	size_t count_attempts (asio::ip::address const & ip) const;

private:
	struct connection
	{
		connection_type type;
		asio::ip::tcp::endpoint endpoint;
		std::shared_ptr<tcp_socket> socket;
		std::shared_ptr<tcp_server> server;

		asio::ip::address address () const
		{
			return endpoint.address ();
		}
	};

	struct attempt
	{
		asio::ip::tcp::endpoint endpoint;
		lumex::async::task task;

		std::chrono::steady_clock::time_point const start{ std::chrono::steady_clock::now () };

		asio::ip::address address () const
		{
			return endpoint.address ();
		}
	};

private:
	uint16_t const port;

	std::list<connection> connections;
	std::list<attempt> attempts;

	lumex::async::strand strand;

	asio::ip::tcp::acceptor acceptor;
	asio::ip::tcp::endpoint local;

	std::atomic<bool> stopped;
	lumex::condition_variable condition;
	mutable lumex::mutex mutex;
	lumex::async::task task;
	std::thread cleanup_thread;

private:
	static lumex::stat::dir to_stat_dir (connection_type);
	static lumex::transport::socket_endpoint to_socket_endpoint (connection_type);
};

std::string_view to_string (tcp_listener::connection_type);
std::string_view to_string (tcp_listener::accept_result);
}
