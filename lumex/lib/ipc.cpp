#include <lumex/lib/ipc.hpp>
#include <lumex/lib/utility.hpp>

/*
 * socket_base
 */

lumex::ipc::socket_base::socket_base (std::shared_ptr<boost::asio::io_context> io_ctx_a) :
	io_ctx{ *io_ctx_a },
	io_ctx_shared{ std::move (io_ctx_a) },
	io_timer{ io_ctx }
{
}

lumex::ipc::socket_base::~socket_base ()
{
	timer_cancel ();
}

void lumex::ipc::socket_base::timer_start (std::chrono::seconds timeout_a)
{
	if (timeout_a < std::chrono::seconds::max ())
	{
		io_timer.expires_after (std::chrono::seconds (timeout_a.count ()));
		io_timer.async_wait ([this] (boost::system::error_code const & ec) {
			if (!ec)
			{
				this->timer_expired ();
			}
		});
	}
}

void lumex::ipc::socket_base::timer_expired ()
{
	close ();
}

void lumex::ipc::socket_base::timer_cancel ()
{
	io_timer.cancel ();
}

/*
 * dsock_file_remover
 */

lumex::ipc::dsock_file_remover::dsock_file_remover (std::string const & file_a) :
	filename (file_a)
{
	std::remove (filename.c_str ());
}

lumex::ipc::dsock_file_remover::~dsock_file_remover ()
{
	std::remove (filename.c_str ());
}
