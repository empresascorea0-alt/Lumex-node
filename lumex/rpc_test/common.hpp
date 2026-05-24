#pragma once

#include <boost/asio/ip/tcp.hpp>

namespace lumex
{
class node;
class node_config;
class node_flags;
class public_key;
class account;

namespace store
{
	class ledger_store;
}

namespace test
{
	class system;
	std::shared_ptr<lumex::node> add_ipc_enabled_node (lumex::test::system & system, lumex::node_config & node_config, lumex::node_flags const & node_flags);
	std::shared_ptr<lumex::node> add_ipc_enabled_node (lumex::test::system & system, lumex::node_config & node_config);
	std::shared_ptr<lumex::node> add_ipc_enabled_node (lumex::test::system & system);
	void reset_confirmation_height (lumex::store::ledger_store & store, lumex::account const & account);
}
}
