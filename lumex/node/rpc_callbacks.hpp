#pragma once

#include <lumex/boost/asio/ip/tcp.hpp>
#include <lumex/lib/interval.hpp>
#include <lumex/lib/thread_pool.hpp>
#include <lumex/node/fwd.hpp>

namespace lumex
{
class http_callbacks
{
public:
	explicit http_callbacks (lumex::node &);

	void start ();
	void stop ();

	lumex::container_info container_info () const;

private: // Dependencies
	lumex::node_config const & config;
	lumex::node & node;
	lumex::node_observers & observers;
	lumex::ledger & ledger;
	lumex::logger & logger;
	lumex::stats & stats;

private:
	void setup_callbacks ();
	void do_rpc_callback (boost::asio::ip::tcp::resolver::results_type::iterator i_a, boost::asio::ip::tcp::resolver::results_type::iterator end_a, std::string const &, uint16_t, std::shared_ptr<std::string> const &, std::shared_ptr<std::string> const &, std::shared_ptr<boost::asio::ip::tcp::resolver> const &, std::shared_ptr<boost::asio::ip::tcp::resolver::results_type> const & results);

	lumex::thread_pool workers;
	lumex::interval_mt warning_interval;
};
}