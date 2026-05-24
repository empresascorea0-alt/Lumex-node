#pragma once
#include <lumex/boost/asio/executor_work_guard.hpp>
#include <lumex/boost/asio/io_context.hpp>
#include <lumex/lib/fwd.hpp>
#include <lumex/lib/thread_roles.hpp>
#include <lumex/lib/threading.hpp>

#include <boost/thread.hpp>

namespace lumex
{
namespace asio = boost::asio;

class thread_runner final
{
public:
	thread_runner (std::shared_ptr<asio::io_context>, lumex::logger &, unsigned num_threads = lumex::hardware_concurrency (), lumex::thread_role::name thread_role = lumex::thread_role::name::io);
	~thread_runner ();

	// Wait for IO threads to complete
	void join ();

	// Tells the IO context to stop processing events.
	// TODO: Ideally this shouldn't be needed, node should stop gracefully by cancelling any outstanding async operations and calling join()
	void abort ();

private:
	void start ();

	unsigned const num_threads;
	lumex::thread_role::name const role;
	lumex::logger & logger;
	std::shared_ptr<asio::io_context> io_ctx;
	asio::executor_work_guard<asio::io_context::executor_type> io_guard;
	std::vector<boost::thread> threads;

private:
	void run ();
};

constexpr unsigned asio_handler_tracking_threshold ()
{
#if LUMEX_ASIO_HANDLER_TRACKING == 0
	return 0;
#else
	return LUMEX_ASIO_HANDLER_TRACKING;
#endif
}
} // namespace lumex
