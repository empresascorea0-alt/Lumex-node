#pragma once

#include <lumex/node/endpoint.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/test_common/system.hpp>

namespace lumex
{
namespace test
{
	class system;
	/** Waits until a TCP connection is established and returns the TCP channel on success*/
	std::shared_ptr<lumex::transport::tcp_channel> establish_tcp (lumex::test::system &, lumex::node &, lumex::endpoint const &);

	/** Adds a node to the system without establishing connections */
	std::shared_ptr<lumex::node> add_outer_node (lumex::test::system & system, lumex::node_config const & config_a, lumex::node_flags const &);
	std::shared_ptr<lumex::node> add_outer_node (lumex::test::system & system, lumex::node_config const & config_a);

	/** Adds a node to the system without establishing connections */
	std::shared_ptr<lumex::node> add_outer_node (lumex::test::system & system, lumex::node_flags const &);
	std::shared_ptr<lumex::node> add_outer_node (lumex::test::system & system);

	/** speculatively (it is not guaranteed that the port will remain free) find a free tcp binding port and return it */
	uint16_t speculatively_choose_a_free_tcp_bind_port ();
}
}
