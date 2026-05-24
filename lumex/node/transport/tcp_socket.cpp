#include <lumex/boost/asio/bind_executor.hpp>
#include <lumex/boost/asio/read.hpp>
#include <lumex/lib/enum_util.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/network_formatting.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/transport/tcp_config.hpp>
#include <lumex/node/transport/tcp_socket.hpp>
#include <lumex/node/transport/transport.hpp>

#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <memory>
#include <utility>

lumex::transport::tcp_socket::tcp_socket (lumex::node & node_a, lumex::transport::socket_endpoint endpoint_type_a) :
	node{ node_a },
	strand{ node_a.io_ctx.get_executor () },
	task{ strand },
	raw_socket{ node_a.io_ctx },
	endpoint_type{ endpoint_type_a }
{
	start ();
}

lumex::transport::tcp_socket::tcp_socket (lumex::node & node_a, asio::ip::tcp::socket raw_socket_a, lumex::transport::socket_endpoint endpoint_type_a) :
	node{ node_a },
	strand{ node_a.io_ctx.get_executor () },
	task{ strand },
	raw_socket{ std::move (raw_socket_a) },
	local_endpoint{ raw_socket.local_endpoint () },
	remote_endpoint{ raw_socket.remote_endpoint () },
	endpoint_type{ endpoint_type_a },
	connected{ true },
	time_connected{ std::chrono::steady_clock::now () }
{
	start ();
}

lumex::transport::tcp_socket::~tcp_socket ()
{
	close ();
}

void lumex::transport::tcp_socket::close ()
{
	stop ();

	if (closed) // Avoid closing the socket multiple times
	{
		return;
	}

	// Node context must be running to gracefully stop async tasks
	debug_assert (!node.io_ctx.stopped ());
	// Ensure that we are not trying to await the task while running on the same thread / io_context
	debug_assert (!node.io_ctx.get_executor ().running_in_this_thread ());

	// Dispatch close raw socket to the strand, wait synchronously for the operation to complete
	auto fut = asio::dispatch (strand, asio::use_future ([this] () {
		close_impl ();
	}));
	fut.wait (); // Blocking call
}

void lumex::transport::tcp_socket::close_async ()
{
	// Node context must be running to gracefully stop async tasks
	debug_assert (!node.io_ctx.stopped ());

	asio::dispatch (strand, [this, /* lifetime guard */ this_s = shared_from_this ()] () {
		close_impl ();
	});
}

void lumex::transport::tcp_socket::close_impl ()
{
	debug_assert (strand.running_in_this_thread ());

	if (closed.exchange (true)) // Avoid closing the socket multiple times
	{
		return;
	}

	boost::system::error_code ec;
	raw_socket.shutdown (asio::ip::tcp::socket::shutdown_both, ec); // Best effort, ignore errors
	raw_socket.close (ec); // Best effort, ignore errors
	if (!ec)
	{
		node.stats.inc (lumex::stat::type::tcp_socket, lumex::stat::detail::close);
		node.logger.debug (lumex::log::type::tcp_socket, "Closed socket: {}", remote_endpoint);
	}
	else
	{
		node.stats.inc (lumex::stat::type::tcp_socket, lumex::stat::detail::close_error);
		node.logger.debug (lumex::log::type::tcp_socket, "Closed socket, ungracefully: {} ({})", remote_endpoint, ec.message ());
	}
}

void lumex::transport::tcp_socket::start ()
{
	release_assert (!task.joinable ());
	task = lumex::async::task (strand, ongoing_checkup ());
}

void lumex::transport::tcp_socket::stop ()
{
	if (task.running ())
	{
		// Node context must be running to gracefully stop async tasks
		debug_assert (!node.io_ctx.stopped ());
		// Ensure that we are not trying to await the task while running on the same thread / io_context
		debug_assert (!node.io_ctx.get_executor ().running_in_this_thread ());

		task.cancel ();
		task.join ();
	}
}

auto lumex::transport::tcp_socket::ongoing_checkup () -> asio::awaitable<void>
{
	debug_assert (strand.running_in_this_thread ());
	try
	{
		while (!co_await lumex::async::cancelled () && alive ())
		{
			bool healthy = checkup ();
			if (!healthy)
			{
				node.stats.inc (lumex::stat::type::tcp_socket, lumex::stat::detail::unhealthy);
				node.logger.debug (lumex::log::type::tcp_socket, "Unhealthy socket detected: {} (timed out: {})",
				remote_endpoint,
				timed_out.load ());

				close_impl ();

				break; // Stop the checkup task
			}
			else
			{
				std::chrono::seconds sleep_duration = node.config.tcp->checkup_interval;
				co_await lumex::async::sleep_for (sleep_duration);
				timestamp += sleep_duration.count ();
			}
		}
	}
	catch (boost::system::system_error const & ex)
	{
		// Operation aborted is expected when cancelling the acceptor
		debug_assert (ex.code () == asio::error::operation_aborted);
	}
	debug_assert (strand.running_in_this_thread ());

	// Close the socket if checkup task is canceled for any reason
	close_impl ();
}

bool lumex::transport::tcp_socket::checkup ()
{
	debug_assert (strand.running_in_this_thread ());

	if (connected)
	{
		if (!raw_socket.is_open ())
		{
			node.stats.inc (lumex::stat::type::tcp_socket, lumex::stat::detail::already_closed);
			return false; // Bad
		}

		debug_assert (timestamp >= read_timestamp);
		debug_assert (timestamp >= write_timestamp);
		debug_assert (timestamp >= last_receive);
		debug_assert (timestamp >= last_send);

		std::chrono::seconds const io_threshold = node.config.tcp->io_timeout;
		std::chrono::seconds const silence_threshold = node.config.tcp->silent_timeout;

		// Timeout threshold of 0 indicates no timeout
		if (io_threshold.count () > 0)
		{
			if (read_timestamp > 0 && timestamp - read_timestamp > io_threshold.count ())
			{
				node.stats.inc (lumex::stat::type::tcp_socket, lumex::stat::detail::timeout);
				node.stats.inc (lumex::stat::type::tcp_socket_timeout, lumex::stat::detail::timeout_receive);
				node.stats.inc (lumex::stat::type::tcp, lumex::stat::detail::tcp_io_timeout_drop, lumex::stat::dir::in);

				timed_out = true;
				return false; // Bad
			}
			if (write_timestamp > 0 && timestamp - write_timestamp > io_threshold.count ())
			{
				node.stats.inc (lumex::stat::type::tcp_socket, lumex::stat::detail::timeout);
				node.stats.inc (lumex::stat::type::tcp_socket_timeout, lumex::stat::detail::timeout_send);
				node.stats.inc (lumex::stat::type::tcp, lumex::stat::detail::tcp_io_timeout_drop, lumex::stat::dir::out);

				timed_out = true;
				return false; // Bad
			}
		}
		if (silence_threshold.count () > 0)
		{
			if ((timestamp - last_receive) > silence_threshold.count () || (timestamp - last_send) > silence_threshold.count ())
			{
				node.stats.inc (lumex::stat::type::tcp_socket, lumex::stat::detail::timeout);
				node.stats.inc (lumex::stat::type::tcp_socket_timeout, lumex::stat::detail::timeout_silence);
				node.stats.inc (lumex::stat::type::tcp, lumex::stat::detail::tcp_silent_connection_drop, lumex::stat::dir::in);

				timed_out = true;
				return false; // Bad
			}
		}
	}
	else // Not connected yet
	{
		auto const now = std::chrono::steady_clock::now ();
		auto const cutoff = now - node.config.tcp->connect_timeout;

		if (time_created < cutoff)
		{
			node.stats.inc (lumex::stat::type::tcp_socket, lumex::stat::detail::timeout);
			node.stats.inc (lumex::stat::type::tcp_socket_timeout, lumex::stat::detail::timeout_connect);

			timed_out = true;
			return false; // Bad
		}
	}

	return true; // Healthy
}

auto lumex::transport::tcp_socket::co_connect (lumex::endpoint endpoint) -> asio::awaitable<std::tuple<boost::system::error_code>>
{
	// Dispatch operation to the strand
	// TODO: This additional dispatch should not be necessary, but it is done during transition to coroutine based code
	co_return co_await asio::co_spawn (strand, co_connect_impl (endpoint), asio::use_awaitable);
}

// TODO: This is only used in tests, remove it, this creates untracked socket
auto lumex::transport::tcp_socket::co_connect_impl (lumex::endpoint endpoint) -> asio::awaitable<std::tuple<boost::system::error_code>>
{
	debug_assert (strand.running_in_this_thread ());
	debug_assert (endpoint_type == socket_endpoint::client);
	debug_assert (!raw_socket.is_open ());
	debug_assert (connect_in_progress.exchange (true) == false);

	auto result = co_await raw_socket.async_connect (endpoint, asio::as_tuple (asio::use_awaitable));
	auto const & [ec] = result;
	if (!ec)
	{
		// Best effort to get the endpoints
		boost::system::error_code ec_ignored;
		local_endpoint = raw_socket.local_endpoint (ec_ignored);
		remote_endpoint = raw_socket.remote_endpoint (ec_ignored);

		connected = true; // Mark as connected
		time_connected = std::chrono::steady_clock::now ();

		node.stats.inc (lumex::stat::type::tcp, lumex::stat::detail::tcp_connect_success);
		node.stats.inc (lumex::stat::type::tcp_socket, lumex::stat::detail::connect_success);
		node.logger.debug (lumex::log::type::tcp_socket, "Successfully connected to: {} from local: {}",
		remote_endpoint, local_endpoint);
	}
	else
	{
		node.stats.inc (lumex::stat::type::tcp, lumex::stat::detail::tcp_connect_error);
		node.stats.inc (lumex::stat::type::tcp_socket, lumex::stat::detail::connect_error);
		node.stats.inc (lumex::stat::type::tcp_socket_connect_ec, lumex::to_stat_detail (ec));
		node.logger.debug (lumex::log::type::tcp_socket, "Failed to connect to: {} ({})",
		endpoint, local_endpoint, ec);

		error = true;
		close_impl ();
	}
	debug_assert (connect_in_progress.exchange (false) == true);
	co_return result;
}

void lumex::transport::tcp_socket::async_connect (lumex::endpoint endpoint, std::function<void (boost::system::error_code const &)> callback)
{
	debug_assert (callback);
	asio::co_spawn (strand, co_connect_impl (endpoint), [callback, /* lifetime guard */ this_s = shared_from_this ()] (std::exception_ptr const & ex, auto const & result) {
		release_assert (!ex);
		auto const & [ec] = result;
		callback (ec);
	});
}

boost::system::error_code lumex::transport::tcp_socket::blocking_connect (lumex::endpoint endpoint)
{
	auto fut = asio::co_spawn (strand, co_connect_impl (endpoint), asio::use_future);
	fut.wait (); // Blocking call
	auto result = fut.get ();
	auto const & [ec] = result;
	return ec;
}

auto lumex::transport::tcp_socket::co_read (lumex::shared_buffer buffer, size_t target_size) -> asio::awaitable<std::tuple<boost::system::error_code, size_t>>
{
	// Dispatch operation to the strand
	// TODO: This additional dispatch should not be necessary, but it is done during transition to coroutine based code
	co_return co_await asio::co_spawn (strand, co_read_impl (buffer, target_size), asio::use_awaitable);
}

auto lumex::transport::tcp_socket::co_read_impl (lumex::shared_buffer buffer, size_t target_size) -> asio::awaitable<std::tuple<boost::system::error_code, size_t>>
{
	debug_assert (strand.running_in_this_thread ());
	debug_assert (read_in_progress.exchange (true) == false);
	release_assert (target_size <= buffer->size (), "read buffer size mismatch");

	read_timestamp = timestamp;
	auto result = co_await asio::async_read (raw_socket, asio::buffer (buffer->data (), target_size), asio::as_tuple (asio::use_awaitable));
	auto const & [ec, size_read] = result;
	read_timestamp = 0;
	if (!ec)
	{
		last_receive = timestamp;
		node.stats.add (lumex::stat::type::traffic_tcp, lumex::stat::detail::all, lumex::stat::dir::in, size_read);
	}
	else
	{
		node.stats.inc (lumex::stat::type::tcp, lumex::stat::detail::tcp_read_error);
		node.stats.inc (lumex::stat::type::tcp_socket_read_ec, lumex::to_stat_detail (ec));
		node.logger.debug (lumex::log::type::tcp_socket, "Error reading from: {} ({})", remote_endpoint, ec);

		error = true;
		close_impl ();
	}
	debug_assert (read_in_progress.exchange (false) == true);
	co_return result;
}

void lumex::transport::tcp_socket::async_read (lumex::shared_buffer buffer, size_t size, std::function<void (boost::system::error_code const &, size_t)> callback)
{
	debug_assert (callback);
	asio::co_spawn (strand, co_read_impl (buffer, size), [callback, /* lifetime guard */ this_s = shared_from_this ()] (std::exception_ptr const & ex, auto const & result) {
		release_assert (!ex);
		auto const & [ec, size] = result;
		callback (ec, size);
	});
}

auto lumex::transport::tcp_socket::blocking_read (lumex::shared_buffer buffer, size_t size) -> std::tuple<boost::system::error_code, size_t>
{
	auto fut = asio::co_spawn (strand, co_read_impl (buffer, size), asio::use_future);
	fut.wait (); // Blocking call
	auto result = fut.get ();
	return result;
}

auto lumex::transport::tcp_socket::co_write (lumex::shared_buffer buffer, size_t target_size) -> asio::awaitable<std::tuple<boost::system::error_code, size_t>>
{
	// Dispatch operation to the strand
	// TODO: This additional dispatch should not be necessary, but it is done during transition to coroutine based code
	co_return co_await asio::co_spawn (strand, co_write_impl (buffer, target_size), asio::use_awaitable);
}

auto lumex::transport::tcp_socket::co_write_impl (lumex::shared_buffer buffer, size_t target_size) -> asio::awaitable<std::tuple<boost::system::error_code, size_t>>
{
	debug_assert (strand.running_in_this_thread ());
	debug_assert (write_in_progress.exchange (true) == false);
	release_assert (target_size <= buffer->size (), "write buffer size mismatch");

	write_timestamp = timestamp;
	auto result = co_await asio::async_write (raw_socket, asio::buffer (buffer->data (), target_size), asio::as_tuple (asio::use_awaitable));
	auto const & [ec, size_written] = result;
	write_timestamp = 0;
	if (!ec)
	{
		last_send = timestamp;
		node.stats.add (lumex::stat::type::traffic_tcp, lumex::stat::detail::all, lumex::stat::dir::out, size_written);
	}
	else
	{
		node.stats.inc (lumex::stat::type::tcp, lumex::stat::detail::tcp_write_error);
		node.stats.inc (lumex::stat::type::tcp_socket_write_ec, lumex::to_stat_detail (ec));
		node.logger.debug (lumex::log::type::tcp_socket, "Error writing to: {} ({})", remote_endpoint, ec);

		error = true;
		close_impl ();
	}
	debug_assert (write_in_progress.exchange (false) == true);
	co_return result;
}

void lumex::transport::tcp_socket::async_write (lumex::shared_buffer buffer, std::function<void (boost::system::error_code const &, size_t)> callback)
{
	debug_assert (callback);
	asio::co_spawn (strand, co_write_impl (buffer, buffer->size ()), [callback, /* lifetime guard */ this_s = shared_from_this ()] (std::exception_ptr const & ex, auto const & result) {
		release_assert (!ex);
		auto const & [ec, size] = result;
		callback (ec, size);
	});
}

auto lumex::transport::tcp_socket::blocking_write (lumex::shared_buffer buffer, size_t size) -> std::tuple<boost::system::error_code, size_t>
{
	auto fut = asio::co_spawn (strand, co_write_impl (buffer, size), asio::use_future);
	fut.wait (); // Blocking call
	auto result = fut.get ();
	return result;
}

lumex::endpoint lumex::transport::tcp_socket::get_remote_endpoint () const
{
	// Using cached value to avoid calling tcp_socket.remote_endpoint() which may be invalid (throw) after closing the socket
	return remote_endpoint;
}

lumex::endpoint lumex::transport::tcp_socket::get_local_endpoint () const
{
	// Using cached value to avoid calling tcp_socket.local_endpoint() which may be invalid (throw) after closing the socket
	return local_endpoint;
}

lumex::transport::socket_endpoint lumex::transport::tcp_socket::get_endpoint_type () const
{
	return endpoint_type;
}

bool lumex::transport::tcp_socket::alive () const
{
	return !closed;
}

bool lumex::transport::tcp_socket::has_connected () const
{
	return connected;
}

bool lumex::transport::tcp_socket::has_timed_out () const
{
	return timed_out;
}

std::chrono::steady_clock::time_point lumex::transport::tcp_socket::get_time_created () const
{
	return time_created;
}

std::chrono::steady_clock::time_point lumex::transport::tcp_socket::get_time_connected () const
{
	return time_connected;
}

void lumex::transport::tcp_socket::operator() (lumex::object_stream & obs) const
{
	obs.write ("remote_endpoint", remote_endpoint);
	obs.write ("local_endpoint", local_endpoint);
	obs.write ("type", type_m.load ());
	obs.write ("endpoint_type", endpoint_type);
}

/*
 *
 */

std::string_view lumex::transport::to_string (socket_type type)
{
	return lumex::enum_to_string (type);
}

std::string_view lumex::transport::to_string (socket_endpoint type)
{
	return lumex::enum_to_string (type);
}
