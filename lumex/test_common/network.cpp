#include <lumex/lib/files.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/transport/tcp_channels.hpp>
#include <lumex/node/transport/transport.hpp>
#include <lumex/test_common/network.hpp>
#include <lumex/test_common/system.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <future>

using namespace std::chrono_literals;

std::shared_ptr<lumex::transport::tcp_channel> lumex::test::establish_tcp (lumex::test::system & system, lumex::node & node, lumex::endpoint const & endpoint)
{
	debug_assert (node.network.endpoint () != endpoint && "Establishing TCP to self is not allowed");

	std::shared_ptr<lumex::transport::tcp_channel> result;
	debug_assert (!node.flags.disable_tcp_realtime);
	node.network.tcp_channels.start_tcp (endpoint);
	auto error = system.poll_until_true (2s, [&result, &node, &endpoint] {
		result = node.network.tcp_channels.find_channel (lumex::transport::map_endpoint_to_tcp (endpoint));
		return result != nullptr;
	});
	return result;
}

// TODO: merge with make_disconnected_node
std::shared_ptr<lumex::node> lumex::test::add_outer_node (lumex::test::system & system_a, lumex::node_config const & config_a, lumex::node_flags const & flags_a)
{
	auto outer_node = std::make_shared<lumex::node> (lumex::unique_path (), config_a, system_a.work, flags_a);
	outer_node->start ();
	system_a.disconnected_nodes.push_back (outer_node);
	return outer_node;
}

std::shared_ptr<lumex::node> lumex::test::add_outer_node (lumex::test::system & system_a, lumex::node_config const & config_a)
{
	return add_outer_node (system_a, config_a, lumex::node_flags{});
}

// TODO: merge with make_disconnected_node
std::shared_ptr<lumex::node> lumex::test::add_outer_node (lumex::test::system & system_a, lumex::node_flags const & flags_a)
{
	auto outer_node = std::make_shared<lumex::node> (system_a.get_available_port (), lumex::unique_path (), system_a.work, flags_a);
	outer_node->start ();
	system_a.disconnected_nodes.push_back (outer_node);
	return outer_node;
}

std::shared_ptr<lumex::node> lumex::test::add_outer_node (lumex::test::system & system_a)
{
	return add_outer_node (system_a, lumex::node_flags{});
}

// Note: this is not guaranteed to work, it is speculative
uint16_t lumex::test::speculatively_choose_a_free_tcp_bind_port ()
{
	/*
	 * This works because the kernel doesn't seem to reuse port numbers until it absolutely has to.
	 * Subsequent binds to port 0 will allocate a different port number.
	 */
	boost::asio::io_context io_ctx;
	boost::asio::ip::tcp::acceptor acceptor{ io_ctx };
	boost::asio::ip::tcp::tcp::endpoint endpoint{ boost::asio::ip::tcp::v4 (), 0 };
	acceptor.open (endpoint.protocol ());

	boost::asio::socket_base::reuse_address option{ true };
	acceptor.set_option (option); // set SO_REUSEADDR option

	acceptor.bind (endpoint);

	auto actual_endpoint = acceptor.local_endpoint ();
	auto port = actual_endpoint.port ();

	acceptor.close ();

	return port;
}
