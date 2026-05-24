#pragma once

#include <lumex/lib/fwd.hpp>
#include <lumex/lib/locks.hpp>
#include <lumex/lib/node_capabilities.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/messages/fwd.hpp>
#include <lumex/messages/keepalive.hpp>
#include <lumex/node/bandwidth_limiter.hpp>
#include <lumex/node/endpoint.hpp>
#include <lumex/node/transport/tcp_socket.hpp>

#include <boost/asio/ip/network_v6.hpp>

namespace lumex::transport
{
enum class transport_type : uint8_t
{
	undefined = 0,
	tcp = 1,
	loopback = 2,
	fake = 3
};

class channel
{
public:
	using callback_t = std::function<void (boost::system::error_code const &, std::size_t)>;

public:
	explicit channel (lumex::node &);
	virtual ~channel () = default;

	/// @returns true if the message was sent (or queued to be sent), false if it was immediately dropped
	bool send (lumex::messages::message const &, lumex::transport::traffic_type, callback_t = nullptr);

	virtual void close () = 0;

	virtual lumex::endpoint get_remote_endpoint () const = 0;
	virtual lumex::endpoint get_local_endpoint () const = 0;

	virtual std::string to_string () const = 0;
	virtual lumex::transport::transport_type get_type () const = 0;

	virtual bool max (lumex::transport::traffic_type)
	{
		return false;
	}

	virtual bool alive () const
	{
		return true;
	}

	std::chrono::steady_clock::time_point get_last_bootstrap_attempt () const
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		return last_bootstrap_attempt;
	}
	void set_last_bootstrap_attempt (std::chrono::steady_clock::time_point const time_a)
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		last_bootstrap_attempt = time_a;
	}

	std::chrono::steady_clock::time_point get_last_packet_received () const
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		return last_packet_received;
	}
	void set_last_packet_received (std::chrono::steady_clock::time_point const time_a)
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		last_packet_received = time_a;
	}

	std::chrono::steady_clock::time_point get_last_packet_sent () const
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		return last_packet_sent;
	}
	void set_last_packet_sent (std::chrono::steady_clock::time_point const time_a)
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		last_packet_sent = time_a;
	}

	std::optional<lumex::account> get_node_id_optional () const
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		return node_id;
	}
	lumex::account get_node_id () const
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		return node_id.value_or (0);
	}
	void set_node_id (lumex::account node_id_a)
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		node_id = node_id_a;
	}

	uint8_t get_network_version () const
	{
		return network_version;
	}
	void set_network_version (uint8_t network_version_a)
	{
		network_version = network_version_a;
	}

	lumex::node_capabilities_flags get_flags () const
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		return flags;
	}
	void set_flags (lumex::node_capabilities_flags value)
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		flags = value;
	}

	lumex::endpoint get_peering_endpoint () const;
	void set_peering_endpoint (lumex::endpoint endpoint);

	void set_last_keepalive (lumex::messages::keepalive const & message);
	std::optional<lumex::messages::keepalive> pop_last_keepalive ();

	std::shared_ptr<lumex::node> owner () const;

protected:
	virtual bool send_impl (lumex::messages::message const &, lumex::transport::traffic_type, callback_t) = 0;

protected:
	lumex::node & node;
	mutable lumex::mutex mutex;

private:
	std::chrono::steady_clock::time_point last_bootstrap_attempt{ std::chrono::steady_clock::time_point () };
	std::chrono::steady_clock::time_point last_packet_received{ std::chrono::steady_clock::now () };
	std::chrono::steady_clock::time_point last_packet_sent{ std::chrono::steady_clock::now () };
	std::optional<lumex::account> node_id{};
	std::atomic<uint8_t> network_version{ 0 };
	lumex::node_capabilities_flags flags;
	std::optional<lumex::endpoint> peering_endpoint{};
	std::optional<lumex::messages::keepalive> last_keepalive{};

public: // Logging
	virtual void operator() (lumex::object_stream &) const;
};
}
