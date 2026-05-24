#pragma once

#include <lumex/lib/async.hpp>
#include <lumex/lib/enum_util.hpp>
#include <lumex/node/transport/channel.hpp>
#include <lumex/node/transport/fwd.hpp>
#include <lumex/node/transport/transport.hpp>

namespace lumex::transport
{
class tcp_channel_queue final
{
public:
	explicit tcp_channel_queue ();

	using callback_t = std::function<void (boost::system::error_code const &, std::size_t)>;
	using entry_t = std::pair<lumex::shared_const_buffer, callback_t>;
	using value_t = std::pair<traffic_type, entry_t>;
	using batch_t = std::deque<value_t>;

	bool empty () const;
	size_t size () const;
	size_t size (traffic_type) const;
	void push (traffic_type, entry_t);
	value_t next ();
	batch_t next_batch (size_t max_count);

	bool max (traffic_type) const;
	bool full (traffic_type) const;

public:
	constexpr static size_t max_size = 32;
	constexpr static size_t full_size = 4 * max_size;

private:
	size_t priority (traffic_type) const;

	using queue_t = std::pair<traffic_type, std::deque<entry_t>>;
	lumex::enum_array<traffic_type, queue_t> queues{};
	lumex::enum_array<traffic_type, queue_t>::iterator current{ queues.end () };
	size_t counter{ 0 };
	size_t total_size{ 0 };
};

class tcp_channel final : public lumex::transport::channel, public std::enable_shared_from_this<tcp_channel>
{
public:
	tcp_channel (lumex::node &, std::shared_ptr<lumex::transport::tcp_socket>);
	~tcp_channel () override;

	void close () override;
	void close_async (); // Safe to call from io context

	bool max (lumex::transport::traffic_type traffic_type) override;
	bool alive () const override;

	lumex::endpoint get_remote_endpoint () const override;
	lumex::endpoint get_local_endpoint () const override;

	lumex::transport::transport_type get_type () const override
	{
		return lumex::transport::transport_type::tcp;
	}

	std::string to_string () const override;

protected:
	bool send_impl (lumex::messages::message const &, lumex::transport::traffic_type, lumex::transport::channel::callback_t) override;

private:
	void start ();
	void stop ();

	asio::awaitable<void> start_sending (lumex::async::condition &);
	asio::awaitable<void> run_sending (lumex::async::condition &);
	asio::awaitable<boost::system::error_code> send_one (traffic_type, tcp_channel_queue::entry_t const &);

public:
	std::shared_ptr<lumex::transport::tcp_socket> socket;

private:
	lumex::endpoint remote_endpoint;
	lumex::endpoint local_endpoint;

	lumex::async::strand strand;
	lumex::async::task sending_task;

	mutable lumex::mutex mutex;
	tcp_channel_queue queue;
	std::atomic<size_t> allocated_bandwidth{ 0 };

public: // Logging
	void operator() (lumex::object_stream &) const override;
};
}
