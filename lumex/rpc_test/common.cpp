#include <lumex/node/ipc/ipc_config.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/rpc_test/common.hpp>
#include <lumex/store/ledger/confirmation_height.hpp>
#include <lumex/store/ledger_store.hpp>
#include <lumex/test_common/system.hpp>
#include <lumex/test_common/testutil.hpp>

std::shared_ptr<lumex::node> lumex::test::add_ipc_enabled_node (lumex::test::system & system, lumex::node_config & node_config, lumex::node_flags const & node_flags)
{
	node_config.ipc_config->transport_tcp.enabled = true;
	node_config.ipc_config->transport_tcp.port = system.get_available_port ();
	return system.add_node (node_config, node_flags);
}

std::shared_ptr<lumex::node> lumex::test::add_ipc_enabled_node (lumex::test::system & system, lumex::node_config & node_config)
{
	return add_ipc_enabled_node (system, node_config, lumex::node_flags ());
}

std::shared_ptr<lumex::node> lumex::test::add_ipc_enabled_node (lumex::test::system & system)
{
	lumex::node_config node_config = system.default_config ();
	return add_ipc_enabled_node (system, node_config);
}

void lumex::test::reset_confirmation_height (lumex::store::ledger_store & store, lumex::account const & account)
{
	auto transaction = store.tx_begin_write ();
	lumex::confirmation_height_info confirmation_height_info;
	if (!store.confirmation_height.get (transaction, account, confirmation_height_info))
	{
		store.confirmation_height.del (transaction, account);
	}
}