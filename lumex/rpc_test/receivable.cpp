#include <lumex/lib/blockbuilders.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/node/backlog_scan.hpp>
#include <lumex/node/ipc/ipc_server.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/wallet.hpp>
#include <lumex/rpc/rpc_request_processor.hpp>
#include <lumex/rpc_test/common.hpp>
#include <lumex/rpc_test/rpc_context.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/store/ledger/confirmation_height.hpp>
#include <lumex/test_common/chains.hpp>
#include <lumex/test_common/system.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/property_tree/json_parser.hpp>

using namespace lumex::test;

TEST (rpc, receivable)
{
	lumex::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto chain = lumex::test::setup_chain (system, *node, 1);
	auto block1 = chain[0];
	ASSERT_TIMELY (5s, node->block_confirmed (block1->hash ()));
	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "receivable");
	request.put ("account", block1->destination ().to_account ());
	auto response = wait_response (system, rpc_ctx, request);
	auto & blocks_node = response.get_child ("blocks");
	ASSERT_EQ (1, blocks_node.size ());
	lumex::block_hash hash{ blocks_node.begin ()->second.get<std::string> ("") };
	ASSERT_EQ (block1->hash (), hash);
}

TEST (rpc, receivable_sorting)
{
	lumex::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto chain = lumex::test::setup_chain (system, *node, 1);
	auto block1 = chain[0];
	ASSERT_TIMELY (5s, node->block_confirmed (block1->hash ()));
	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "receivable");
	request.put ("account", block1->destination ().to_account ());
	request.put ("sorting", "true"); // Sorting test
	auto response = wait_response (system, rpc_ctx, request);
	auto & blocks_node = response.get_child ("blocks");
	ASSERT_EQ (1, blocks_node.size ());
	lumex::block_hash hash{ blocks_node.begin ()->first };
	ASSERT_EQ (block1->hash (), hash);
	std::string amount{ blocks_node.begin ()->second.get<std::string> ("") };
	ASSERT_EQ ("1", amount);
}

TEST (rpc, receivable_threshold_sufficient)
{
	lumex::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto chain = lumex::test::setup_chain (system, *node, 1);
	auto block1 = chain[0];
	ASSERT_TIMELY (5s, node->block_confirmed (block1->hash ()));
	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "receivable");
	request.put ("account", block1->destination ().to_account ());
	request.put ("threshold", "1"); // Threshold test
	auto response = wait_response (system, rpc_ctx, request);
	auto & blocks_node = response.get_child ("blocks");
	ASSERT_EQ (1, blocks_node.size ());
	std::unordered_map<lumex::block_hash, lumex::uint128_union> blocks;
	for (auto i (blocks_node.begin ()), j (blocks_node.end ()); i != j; ++i)
	{
		lumex::block_hash hash;
		hash.decode_hex (i->first);
		lumex::uint128_union amount;
		amount.decode_dec (i->second.get<std::string> (""));
		blocks[hash] = amount;
		auto source = i->second.get_optional<std::string> ("source");
		ASSERT_FALSE (source.has_value ());
		auto min_version = i->second.get_optional<uint8_t> ("min_version");
		ASSERT_FALSE (min_version.has_value ());
	}
	ASSERT_EQ (blocks[block1->hash ()], 1);
}

TEST (rpc, receivable_threshold_insufficient)
{
	lumex::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto chain = lumex::test::setup_chain (system, *node, 1);
	auto block1 = chain[0];
	ASSERT_TIMELY (5s, node->block_confirmed (block1->hash ()));
	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "receivable");
	request.put ("account", block1->destination ().to_account ());
	request.put ("threshold", "2"); // Chains are set up with 1 raw transfers therefore all blocks are less than 2 raw.
	auto response = wait_response (system, rpc_ctx, request, 10s);
	auto & blocks_node = response.get_child ("blocks");
	ASSERT_EQ (0, blocks_node.size ());
}

TEST (rpc, receivable_source_min_version)
{
	lumex::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto chain = lumex::test::setup_chain (system, *node, 1);
	auto block1 = chain[0];
	ASSERT_TIMELY (5s, node->block_confirmed (block1->hash ()));
	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "receivable");
	request.put ("account", block1->destination ().to_account ());
	request.put ("source", "true");
	request.put ("min_version", "true");
	auto response (wait_response (system, rpc_ctx, request));
	auto & blocks_node (response.get_child ("blocks"));
	ASSERT_EQ (1, blocks_node.size ());
	std::unordered_map<lumex::block_hash, lumex::uint128_union> amounts;
	std::unordered_map<lumex::block_hash, lumex::account> sources;
	for (auto i (blocks_node.begin ()), j (blocks_node.end ()); i != j; ++i)
	{
		lumex::block_hash hash;
		hash.decode_hex (i->first);
		amounts[hash].decode_dec (i->second.get<std::string> ("amount"));
		sources[hash].decode_account (i->second.get<std::string> ("source"));
		ASSERT_EQ (i->second.get<uint8_t> ("min_version"), 0);
	}
	ASSERT_EQ (amounts[block1->hash ()], 1);
	ASSERT_EQ (sources[block1->hash ()], lumex::dev::genesis_key.pub);
}

TEST (rpc, receivable_unconfirmed)
{
	lumex::test::system system;
	lumex::node_config config;
	config.backlog_scan->enable = false;
	auto node = add_ipc_enabled_node (system, config);
	auto chain = lumex::test::setup_chain (system, *node, 1, lumex::dev::genesis_key, false);
	auto block1 = chain[0];

	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "receivable");
	request.put ("account", block1->destination ().to_account ());
	ASSERT_TRUE (check_block_response_count (system, rpc_ctx, request, 0));
	request.put ("include_only_confirmed", "true");
	ASSERT_TRUE (check_block_response_count (system, rpc_ctx, request, 0));
	request.put ("include_only_confirmed", "false");
	ASSERT_TRUE (check_block_response_count (system, rpc_ctx, request, 1));
	lumex::test::confirm (node->ledger, block1);
	request.put ("include_only_confirmed", "true");
	ASSERT_TRUE (check_block_response_count (system, rpc_ctx, request, 1));
}

/*TEST (rpc, amounts)
{
	auto block2 (system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key1.pub, 200));
	auto block3 (system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key1.pub, 300));
	auto block4 (system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key1.pub, 400));
	rpc_ctx.io_scope->renew ();

	ASSERT_TIMELY_EQ (10s, node->ledger.account_receivable (node->store.tx_begin_read (), key1.pub), 1000);
	ASSERT_TIMELY (5s, !node->active.active (*block4));
	ASSERT_TIMELY (5s, node->block_confirmed (block4->hash ()));

	request.put ("count", "2");
	{
		auto response (wait_response (system, rpc_ctx, request));
		auto & blocks_node (response.get_child ("blocks"));
		ASSERT_EQ (2, blocks_node.size ());
		lumex::block_hash hash (blocks_node.begin ()->first);
		lumex::block_hash hash1 ((++blocks_node.begin ())->first);
		ASSERT_EQ (block4->hash (), hash);
		ASSERT_EQ (block3->hash (), hash1);
	}
}*/

/**
 * This test case tests the receivable RPC command when used with offsets and sorting.
 */
TEST (rpc, receivable_offset_and_sorting)
{
	lumex::test::system system;
	auto node = add_ipc_enabled_node (system);
	lumex::keypair key1;
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);

	auto block1 = system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key1.pub, 200);
	auto block2 = system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key1.pub, 100);
	auto block3 = system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key1.pub, 400);
	auto block4 = system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key1.pub, 300);
	auto block5 = system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key1.pub, 300);
	auto block6 = system.wallet (0)->send_action (lumex::dev::genesis_key.pub, key1.pub, 300);

	// check that all blocks got confirmed
	ASSERT_TIMELY_EQ (5s, node->ledger.account_receivable (node->ledger.tx_begin_read (), key1.pub, true), 1600);

	// check confirmation height is as expected, there is no perfect clarity yet when confirmation height updates after a block get confirmed
	lumex::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (node->store.confirmation_height.get (node->store.tx_begin_read (), lumex::dev::genesis_key.pub, confirmation_height_info));
	ASSERT_EQ (confirmation_height_info.height, 7);
	ASSERT_EQ (confirmation_height_info.frontier, block6->hash ());

	// returns true if hash is found in node
	// if match_first is set then the function looks for key (first item)
	// if match_first is not set then the function looks for value (second item)
	auto hash_exists = [] (boost::property_tree::ptree & node, bool match_first, lumex::block_hash hash) {
		std::stringstream ss;
		boost::property_tree::json_parser::write_json (ss, node);
		for (auto itr = node.begin (); itr != node.end (); ++itr)
		{
			std::string possible_match = match_first ? itr->first : itr->second.get<std::string> ("");
			if (possible_match == hash.to_string ())
			{
				return true;
			}
		}
		return false;
	};

	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "receivable");
	request.put ("account", key1.pub.to_account ());

	request.put ("offset", "0");
	request.put ("sorting", "false");
	{
		auto response (wait_response (system, rpc_ctx, request));
		auto & blocks_node (response.get_child ("blocks"));
		ASSERT_EQ (6, blocks_node.size ());

		// check that all 6 blocks are listed, the order does not matter
		ASSERT_TRUE (hash_exists (blocks_node, false, block1->hash ()));
		ASSERT_TRUE (hash_exists (blocks_node, false, block2->hash ()));
		ASSERT_TRUE (hash_exists (blocks_node, false, block3->hash ()));
		ASSERT_TRUE (hash_exists (blocks_node, false, block4->hash ()));
		ASSERT_TRUE (hash_exists (blocks_node, false, block5->hash ()));
		ASSERT_TRUE (hash_exists (blocks_node, false, block6->hash ()));
	}

	request.put ("offset", "4");
	{
		auto response (wait_response (system, rpc_ctx, request));
		auto & blocks_node (response.get_child ("blocks"));
		// since we haven't asked for sorted, we can't be sure which 2 blocks will be returned
		ASSERT_EQ (2, blocks_node.size ());
	}

	request.put ("count", "2");
	request.put ("offset", "2");
	{
		auto response (wait_response (system, rpc_ctx, request));
		auto & blocks_node (response.get_child ("blocks"));
		// since we haven't asked for sorted, we can't be sure which 2 blocks will be returned
		ASSERT_EQ (2, blocks_node.size ());
	}

	// Sort by amount from here onwards, this is a sticky setting that applies for the rest of the test case
	request.put ("sorting", "true");

	request.put ("count", "5");
	request.put ("offset", "0");
	{
		auto response (wait_response (system, rpc_ctx, request));
		auto & blocks_node (response.get_child ("blocks"));
		ASSERT_EQ (5, blocks_node.size ());

		// the first block should be block3 with amount 400
		auto itr = blocks_node.begin ();
		ASSERT_EQ (block3->hash (), lumex::block_hash{ itr->first });
		ASSERT_EQ ("400", itr->second.get<std::string> (""));

		// the next 3 block will be of amount 300 but in unspecified order
		++itr;
		ASSERT_EQ ("300", itr->second.get<std::string> (""));

		++itr;
		ASSERT_EQ ("300", itr->second.get<std::string> (""));

		++itr;
		ASSERT_EQ ("300", itr->second.get<std::string> (""));

		// the last one will be block1 with amount 200
		++itr;
		ASSERT_EQ (block1->hash (), lumex::block_hash{ itr->first });
		ASSERT_EQ ("200", itr->second.get<std::string> (""));

		// check that the blocks returned with 300 amounts have the right hashes
		ASSERT_TRUE (hash_exists (blocks_node, true, block4->hash ()));
		ASSERT_TRUE (hash_exists (blocks_node, true, block5->hash ()));
		ASSERT_TRUE (hash_exists (blocks_node, true, block6->hash ()));
	}

	request.put ("count", "3");
	request.put ("offset", "3");
	{
		auto response (wait_response (system, rpc_ctx, request));
		auto & blocks_node (response.get_child ("blocks"));
		ASSERT_EQ (3, blocks_node.size ());

		auto itr = blocks_node.begin ();
		ASSERT_EQ ("300", itr->second.get<std::string> (""));

		++itr;
		ASSERT_EQ (block1->hash (), lumex::block_hash{ itr->first });
		ASSERT_EQ ("200", itr->second.get<std::string> (""));

		++itr;
		ASSERT_EQ (block2->hash (), lumex::block_hash{ itr->first });
		ASSERT_EQ ("100", itr->second.get<std::string> (""));
	}

	request.put ("source", "true");
	request.put ("min_version", "true");
	request.put ("count", "3");
	request.put ("offset", "2");
	{
		auto response (wait_response (system, rpc_ctx, request));
		auto & blocks_node (response.get_child ("blocks"));
		ASSERT_EQ (3, blocks_node.size ());

		auto itr = blocks_node.begin ();
		ASSERT_EQ ("300", itr->second.get<std::string> ("amount"));

		++itr;
		ASSERT_EQ ("300", itr->second.get<std::string> ("amount"));

		++itr;
		ASSERT_EQ (block1->hash (), lumex::block_hash{ itr->first });
		ASSERT_EQ ("200", itr->second.get<std::string> ("amount"));
	}
}

TEST (rpc, receivable_burn)
{
	lumex::test::system system;
	auto node = add_ipc_enabled_node (system);
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	auto block1 (system.wallet (0)->send_action (lumex::dev::genesis_key.pub, lumex::dev::constants.burn_account, 100));
	auto const rpc_ctx = add_rpc (system, node);
	ASSERT_TIMELY (5s, node->block_confirmed (block1->hash ()));
	boost::property_tree::ptree request;
	request.put ("action", "receivable");
	request.put ("account", lumex::dev::constants.burn_account.to_account ());
	request.put ("count", "100");
	{
		auto response (wait_response (system, rpc_ctx, request));
		auto & blocks_node (response.get_child ("blocks"));
		ASSERT_EQ (1, blocks_node.size ());
		lumex::block_hash hash (blocks_node.begin ()->second.get<std::string> (""));
		ASSERT_EQ (block1->hash (), hash);
	}
}

TEST (rpc, search_receivable)
{
	lumex::test::system system;
	auto node = add_ipc_enabled_node (system);
	system.wallet (0)->insert_adhoc (lumex::dev::genesis_key.prv);
	auto wallet (node->wallets.items.begin ()->first.to_string ());
	auto latest (node->latest (lumex::dev::genesis_key.pub));
	lumex::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (latest)
				 .destination (lumex::dev::genesis_key.pub)
				 .balance (lumex::dev::constants.genesis_amount - node->config.receive_minimum.number ())
				 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				 .work (*node->work_generate_blocking (latest))
				 .build ();
	{
		auto transaction = node->ledger.tx_begin_write ();
		ASSERT_EQ (lumex::block_status::progress, node->ledger.process (transaction, block));
	}
	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "search_receivable");
	request.put ("wallet", wallet);
	auto response (wait_response (system, rpc_ctx, request));
	ASSERT_TIMELY_EQ (10s, node->balance (lumex::dev::genesis_key.pub), lumex::dev::constants.genesis_amount);
}

TEST (rpc, accounts_pending_deprecated)
{
	lumex::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	boost::property_tree::ptree child;
	boost::property_tree::ptree accounts;
	child.put ("", lumex::dev::genesis_key.pub.to_account ());
	accounts.push_back (std::make_pair ("", child));
	request.add_child ("accounts", accounts);
	request.put ("action", "accounts_pending");
	auto response (wait_response (system, rpc_ctx, request));
	ASSERT_EQ ("1", response.get<std::string> ("deprecated"));
}

TEST (rpc, accounts_receivable_blocks)
{
	lumex::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto chain = lumex::test::setup_chain (system, *node, 1);
	auto block1 = chain[0];
	ASSERT_TIMELY (5s, node->block_confirmed (block1->hash ()));

	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "accounts_receivable");
	boost::property_tree::ptree entry;
	boost::property_tree::ptree peers_l;
	entry.put ("", block1->destination ().to_account ());
	peers_l.push_back (std::make_pair ("", entry));
	request.add_child ("accounts", peers_l);
	auto response = wait_response (system, rpc_ctx, request);
	for (auto & blocks : response.get_child ("blocks"))
	{
		std::string account_text{ blocks.first };
		ASSERT_EQ (block1->destination ().to_account (), account_text);
		lumex::block_hash hash1{ blocks.second.begin ()->second.get<std::string> ("") };
		ASSERT_EQ (block1->hash (), hash1);
	}
}

TEST (rpc, accounts_receivable_sorting)
{
	lumex::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto chain = lumex::test::setup_chain (system, *node, 1);
	auto block1 = chain[0];
	ASSERT_TIMELY (5s, node->block_confirmed (block1->hash ()));

	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "accounts_receivable");
	boost::property_tree::ptree entry;
	boost::property_tree::ptree peers_l;
	entry.put ("", block1->destination ().to_account ());
	peers_l.push_back (std::make_pair ("", entry));
	request.add_child ("accounts", peers_l);
	request.put ("sorting", "true"); // Sorting test
	auto response = wait_response (system, rpc_ctx, request);
	for (auto & blocks : response.get_child ("blocks"))
	{
		std::string account_text{ blocks.first };
		ASSERT_EQ (block1->destination ().to_account (), account_text);
		lumex::block_hash hash1{ blocks.second.begin ()->first };
		ASSERT_EQ (block1->hash (), hash1);
		std::string amount{ blocks.second.begin ()->second.get<std::string> ("") };
		ASSERT_EQ ("1", amount);
	}
}

TEST (rpc, accounts_receivable_threshold)
{
	lumex::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto chain = lumex::test::setup_chain (system, *node, 1);
	auto block1 = chain[0];
	ASSERT_TIMELY (5s, node->block_confirmed (block1->hash ()));

	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "accounts_receivable");
	boost::property_tree::ptree entry;
	boost::property_tree::ptree peers_l;
	entry.put ("", block1->destination ().to_account ());
	peers_l.push_back (std::make_pair ("", entry));
	request.add_child ("accounts", peers_l);
	request.put ("threshold", "1"); // Threshold test
	auto response = wait_response (system, rpc_ctx, request);
	std::unordered_map<lumex::block_hash, lumex::uint128_union> blocks;
	for (auto & pending : response.get_child ("blocks"))
	{
		std::string account_text{ pending.first };
		ASSERT_EQ (block1->destination ().to_account (), account_text);
		for (auto i (pending.second.begin ()), j (pending.second.end ()); i != j; ++i)
		{
			lumex::block_hash hash;
			hash.decode_hex (i->first);
			lumex::uint128_union amount;
			amount.decode_dec (i->second.get<std::string> (""));
			blocks[hash] = amount;
			auto source = i->second.get_optional<std::string> ("source");
			ASSERT_FALSE (source.has_value ());
		}
	}
	ASSERT_EQ (blocks[block1->hash ()], 1);
}

TEST (rpc, accounts_receivable_source)
{
	lumex::test::system system;
	auto node = add_ipc_enabled_node (system);
	auto chain = lumex::test::setup_chain (system, *node, 1, lumex::dev::genesis_key);
	auto block1 = chain[0];

	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "accounts_receivable");
	boost::property_tree::ptree entry;
	boost::property_tree::ptree peers_l;
	entry.put ("", block1->destination ().to_account ());
	peers_l.push_back (std::make_pair ("", entry));
	request.add_child ("accounts", peers_l);
	request.put ("source", "true");
	{
		auto response (wait_response (system, rpc_ctx, request));
		std::unordered_map<lumex::block_hash, lumex::uint128_union> amounts;
		std::unordered_map<lumex::block_hash, lumex::account> sources;
		for (auto & pending : response.get_child ("blocks"))
		{
			std::string account_text (pending.first);
			ASSERT_EQ (block1->destination ().to_account (), account_text);
			for (auto i (pending.second.begin ()), j (pending.second.end ()); i != j; ++i)
			{
				lumex::block_hash hash;
				hash.decode_hex (i->first);
				amounts[hash].decode_dec (i->second.get<std::string> ("amount"));
				sources[hash].decode_account (i->second.get<std::string> ("source"));
			}
		}
		ASSERT_EQ (amounts[block1->hash ()], 1);
		ASSERT_EQ (sources[block1->hash ()], lumex::dev::genesis_key.pub);
	}
}

TEST (rpc, accounts_receivable_confirmed)
{
	lumex::test::system system;
	lumex::node_config config;
	config.backlog_scan->enable = false;
	auto node = add_ipc_enabled_node (system, config);
	auto chain = lumex::test::setup_chain (system, *node, 1, lumex::dev::genesis_key, false);
	auto block1 = chain[0];

	auto const rpc_ctx = add_rpc (system, node);
	boost::property_tree::ptree request;
	request.put ("action", "accounts_receivable");
	boost::property_tree::ptree entry;
	boost::property_tree::ptree peers_l;
	entry.put ("", block1->destination ().to_account ());
	peers_l.push_back (std::make_pair ("", entry));
	request.add_child ("accounts", peers_l);

	ASSERT_TRUE (check_block_response_count (system, rpc_ctx, request, 0));
	request.put ("include_only_confirmed", "true");
	ASSERT_TRUE (check_block_response_count (system, rpc_ctx, request, 0));
	request.put ("include_only_confirmed", "false");
	ASSERT_TRUE (check_block_response_count (system, rpc_ctx, request, 1));
	lumex::test::confirm (node->ledger, block1);
	request.put ("include_only_confirmed", "true");
	ASSERT_TRUE (check_block_response_count (system, rpc_ctx, request, 1));
}
