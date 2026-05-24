#include <lumex/crypto_lib/random_pool.hpp>
#include <lumex/lib/blockbuilders.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/files.hpp>
#include <lumex/lib/formatting.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/thread_runner.hpp>
#include <lumex/lib/work_version.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/endpoint.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/transport/tcp_listener.hpp>
#include <lumex/node/wallet.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_set_any.hpp>
#include <lumex/store/ledger/account.hpp>
#include <lumex/test_common/common.hpp>
#include <lumex/test_common/system.hpp>
#include <lumex/test_common/testutil.hpp>

#include <boost/asio.hpp>
#include <boost/format.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <cstdlib>

using namespace std::chrono_literals;

std::string lumex::error_system_messages::message (int ev) const
{
	switch (static_cast<lumex::error_system> (ev))
	{
		case lumex::error_system::generic:
			return "Unknown error";
		case lumex::error_system::deadline_expired:
			return "Deadline expired";
	}

	return "Invalid error code";
}

/*
 * system
 */

lumex::test::system::system () :
	io_ctx{ std::make_shared<boost::asio::io_context> () },
	io_guard{ boost::asio::make_work_guard (*io_ctx) },
	logger{ "test" },
	stats{ logger }
{
	auto scale_str = std::getenv ("DEADLINE_SCALE_FACTOR");
	if (scale_str)
	{
		deadline_scaling_factor = std::stod (scale_str);
	}
}

lumex::test::system::system (uint16_t count_a, lumex::transport::transport_type type_a) :
	system (count_a, type_a, lumex::node_flags{})
{
}

lumex::test::system::system (uint16_t count_a, lumex::transport::transport_type type_a, lumex::node_flags const & flags_a) :
	system ()
{
	nodes.reserve (count_a);
	for (uint16_t i (0); i < count_a; ++i)
	{
		add_node (default_config (), flags_a, type_a);
	}
}

lumex::test::system::~system ()
{
	// Only stop system in destructor to avoid confusing and random bugs when debugging assertions that hit deadline expired condition
	stop ();

#ifndef _WIN32
	// Windows cannot remove the log and data files while they are still owned by this process.
	// They will be removed later

	// Clean up tmp directories created by the tests. Since it's sometimes useful to
	// see log files after test failures, an environment variable is supported to
	// retain the files.
	if (std::getenv ("TEST_KEEP_TMPDIRS") == nullptr)
	{
		lumex::remove_temporary_directories ();
	}
#endif
}

void lumex::test::system::stop ()
{
	logger.debug (lumex::log::type::system, "Stopping...");

	// Keep io_context running while stopping nodes
	for (auto & node : nodes)
	{
		stop_node (*node);
	}
	for (auto & node : disconnected_nodes)
	{
		stop_node (*node);
	}

	stats.stop ();
	io_guard.reset ();
	work.stop ();
}

void lumex::test::system::set_initialization_blocks (std::deque<std::shared_ptr<lumex::block>> blocks)
{
	this->initialization_blocks = std::move (blocks);
}

void lumex::test::system::set_cemented_initialization_blocks (std::deque<std::shared_ptr<lumex::block>> blocks)
{
	this->initialization_blocks_cemented = std::move (blocks);
}

lumex::node & lumex::test::system::node (std::size_t index) const
{
	debug_assert (index < nodes.size ());
	return *nodes[index];
}

std::shared_ptr<lumex::node> lumex::test::system::add_node (lumex::transport::transport_type type_a)
{
	return add_node (default_config (), lumex::node_flags{}, type_a);
}

std::shared_ptr<lumex::node> lumex::test::system::add_node (lumex::node_flags const & node_flags_a, lumex::transport::transport_type type_a)
{
	return add_node (default_config (), node_flags_a, type_a);
}

std::shared_ptr<lumex::node> lumex::test::system::add_node (lumex::node_config const & node_config_a, lumex::transport::transport_type type_a)
{
	return add_node (node_config_a, lumex::node_flags{}, type_a);
}

/** Returns the node added. */
std::shared_ptr<lumex::node> lumex::test::system::add_node (lumex::node_config const & node_config_a, lumex::node_flags const & node_flags_a, lumex::transport::transport_type type_a, std::optional<lumex::keypair> const & rep)
{
	auto node (std::make_shared<lumex::node> (lumex::unique_path (), node_config_a, work, node_flags_a, node_sequence++));
	setup_node (*node);
	auto wallet = node->wallets.create (lumex::random_wallet_id ());
	if (rep)
	{
		auto result = wallet->insert_adhoc (rep->prv);
		debug_assert (result);
	}
	node->start ();

	// Check that we don't start more nodes than limit for single IP address
	debug_assert (nodes.size () < node->config.network->max_peers_per_ip || node->flags.disable_max_peers_per_ip);

	// Connect with other nodes
	for (auto const & other_node : nodes)
	{
		if (other_node->stopped)
		{
			continue;
		}

		logger.debug (lumex::log::type::system, "Connecting nodes: {} and {}", node->identifier (), other_node->identifier ());

		// TCP is the only transport layer available.
		debug_assert (type_a == lumex::transport::transport_type::tcp);
		node->network.merge_peer (other_node->network.endpoint ());

		auto ec = poll_until_true (5s, [&] () {
			bool result_1 = node->network.find_node_id (other_node->get_node_id ()) != nullptr;
			bool result_2 = other_node->network.find_node_id (node->get_node_id ()) != nullptr;
			return result_1 && result_2;
		});
		debug_assert (!ec);
	}

	logger.debug (lumex::log::type::system, "Node started: {}", lumex::log::as_node_id (node->get_node_id ()));

	nodes.push_back (node);
	return node;
}

std::shared_ptr<lumex::node> lumex::test::system::make_disconnected_node ()
{
	return make_disconnected_node (default_config (), lumex::node_flags{});
}

// TODO: Merge with add_node
std::shared_ptr<lumex::node> lumex::test::system::make_disconnected_node (lumex::node_config const & node_config, lumex::node_flags const & flags)
{
	auto node = std::make_shared<lumex::node> (lumex::unique_path (), node_config, work, flags);
	setup_node (*node);
	node->start ();

	logger.debug (lumex::log::type::system, "Node started (disconnected): {}", lumex::log::as_node_id (node->get_node_id ()));

	disconnected_nodes.push_back (node);
	return node;
}

void lumex::test::system::setup_node (lumex::node & node)
{
	auto transaction = node.ledger.tx_begin_write ();

	for (auto block : initialization_blocks)
	{
		auto result = node.ledger.process (transaction, block);
		debug_assert (result == lumex::block_status::progress);
	}

	for (auto block : initialization_blocks_cemented)
	{
		auto result = node.ledger.process (transaction, block);
		debug_assert (result == lumex::block_status::progress);

		auto cemented = node.ledger.cement (transaction, block->hash ());
		debug_assert (std::find_if (cemented.begin (), cemented.end (), [&block] (auto const & cemented_block) {
			return cemented_block->hash () == block->hash ();
		})
		!= cemented.end ());
	}
}

void lumex::test::system::register_node (std::shared_ptr<lumex::node> const & node)
{
	debug_assert (std::find (nodes.begin (), nodes.end (), node) == nodes.end ());
	nodes.push_back (node);
}

void lumex::test::system::stop_node (lumex::node & node)
{
	auto stopped = std::async (std::launch::async, [&node] () {
		node.stop ();
	});
	auto ec = poll_until_true (5s, [&] () {
		auto status = stopped.wait_for (0s);
		return status == std::future_status::ready;
	});
	debug_assert (!ec);
}

void lumex::test::system::ledger_initialization_set (std::deque<lumex::keypair> const & reps, lumex::amount const & reserve)
{
	lumex::block_hash previous = lumex::dev::genesis->hash ();
	auto amount = (lumex::dev::constants.genesis_amount - reserve.number ()) / reps.size ();
	auto balance = lumex::dev::constants.genesis_amount;
	for (auto const & i : reps)
	{
		balance -= amount;
		lumex::state_block_builder builder;
		builder.account (lumex::dev::genesis_key.pub)
		.previous (previous)
		.representative (lumex::dev::genesis_key.pub)
		.link (i.pub)
		.balance (balance)
		.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
		.work (*work.generate (previous));
		initialization_blocks.emplace_back (builder.build ());
		previous = initialization_blocks.back ()->hash ();
		builder.make_block ();
		builder.account (i.pub)
		.previous (0)
		.representative (i.pub)
		.link (previous)
		.balance (amount)
		.sign (i.prv, i.pub)
		.work (*work.generate (i.pub));
		initialization_blocks.emplace_back (builder.build ());
	}
}

std::shared_ptr<lumex::wallet::wallet> lumex::test::system::wallet (size_t index_a)
{
	debug_assert (nodes.size () > index_a);
	auto size (nodes[index_a]->wallets.items.size ());
	(void)size;
	debug_assert (size == 1);
	return nodes[index_a]->wallets.items.begin ()->second;
}

uint64_t lumex::test::system::work_generate_limited (lumex::block_hash const & root_a, uint64_t min_a, uint64_t max_a)
{
	debug_assert (min_a > 0);
	uint64_t result = 0;
	do
	{
		result = *work.generate (root_a, min_a);
	} while (work.network_constants.work.difficulty (lumex::work_version::work_1, root_a, result) >= max_a);
	return result;
}

/** Initiate an epoch upgrade. Writes the epoch block into the ledger and leaves it to
 *  node background processes (e.g. frontiers confirmation) to cement the block.
 */
std::shared_ptr<lumex::state_block> lumex::test::upgrade_epoch (lumex::work_pool & pool_a, lumex::ledger & ledger_a, lumex::epoch epoch_a)
{
	auto transaction = ledger_a.tx_begin_write ();
	auto dev_genesis_key = lumex::dev::genesis_key;
	auto account = dev_genesis_key.pub;
	auto latest = ledger_a.any.account_head (transaction, account);
	auto balance = ledger_a.any.account_balance (transaction, account).value_or (0);

	lumex::state_block_builder builder;
	std::error_code ec;
	auto epoch = builder
				 .account (dev_genesis_key.pub)
				 .previous (latest)
				 .balance (balance)
				 .link (ledger_a.epoch_link (epoch_a))
				 .representative (dev_genesis_key.pub)
				 .sign (dev_genesis_key.prv, dev_genesis_key.pub)
				 .work (*pool_a.generate (latest, pool_a.network_constants.work.threshold (lumex::work_version::work_1, lumex::block_details (epoch_a, false, false, true))))
				 .build (ec);

	bool error{ true };
	if (!ec && epoch)
	{
		error = ledger_a.process (transaction, epoch) != lumex::block_status::progress;
	}

	return !error ? std::move (epoch) : nullptr;
}

std::shared_ptr<lumex::state_block> lumex::test::system::upgrade_genesis_epoch (lumex::node & node_a, lumex::epoch const epoch_a)
{
	return upgrade_epoch (work, node_a.ledger, epoch_a);
}

void lumex::test::system::deadline_set (std::chrono::duration<double, std::lumex> const & delta_a)
{
	deadline = std::chrono::steady_clock::now () + delta_a * deadline_scaling_factor;
}

std::error_code lumex::test::system::poll (std::chrono::lumexseconds const & wait_time)
{
	if constexpr (lumex::asio_handler_tracking_threshold () == 0)
	{
		io_ctx->run_one_for (wait_time);
	}
	else
	{
		lumex::timer<> timer;
		timer.start ();
		auto count = io_ctx->poll_one ();
		if (count == 0)
		{
			std::this_thread::sleep_for (wait_time);
		}
		else if (count == 1 && timer.since_start ().count () >= lumex::asio_handler_tracking_threshold ())
		{
			logger.warn (lumex::log::type::system, "Async handler processing took too long: {}ms", timer.since_start ().count ());
		}
	}

	std::this_thread::yield ();

	std::error_code ec;
	if (std::chrono::steady_clock::now () > deadline)
	{
		ec = lumex::error_system::deadline_expired;
	}
	return ec;
}

std::error_code lumex::test::system::poll_until_true (std::chrono::lumexseconds deadline_a, std::function<bool ()> predicate_a)
{
	std::error_code ec;
	deadline_set (deadline_a);
	while (!ec && !predicate_a ())
	{
		try
		{
			ec = poll ();
		}
		catch (std::exception const & ex)
		{
			std::cerr << "Error running IO: " << ex.what () << std::endl;
			ec = lumex::error_system::generic;
		}
		catch (...)
		{
			std::cerr << "Unknown error running IO" << std::endl;
			ec = lumex::error_system::generic;
		}
	}
	return ec;
}

/**
 * This function repetitively calls io_ctx.run_one_for until delay number of milliseconds elapse.
 * It can be used to sleep for a duration in unit tests whilst allowing the background io contexts to continue processing.
 * @param delay milliseconds of delay
 */
void lumex::test::system::delay_ms (std::chrono::milliseconds const & delay)
{
	auto now = std::chrono::steady_clock::now ();
	auto endtime = now + delay;
	while (now <= endtime)
	{
		io_ctx->run_one_for (endtime - now);
		now = std::chrono::steady_clock::now ();
	}
}

namespace
{
class traffic_generator : public std::enable_shared_from_this<traffic_generator>
{
public:
	traffic_generator (uint32_t count_a, uint32_t wait_a, std::shared_ptr<lumex::node> const & node_a, lumex::test::system & system_a) :
		count (count_a),
		wait (wait_a),
		node (node_a),
		system (system_a)
	{
	}
	void run ()
	{
		auto count_l (count - 1);
		count = count_l - 1;
		system.generate_activity (*node, accounts);
		if (count_l > 0)
		{
			auto this_l (shared_from_this ());
			node->workers.post_delayed (std::chrono::milliseconds (wait), [this_l] () { this_l->run (); });
		}
	}
	std::vector<lumex::account> accounts;
	uint32_t count;
	uint32_t wait;
	std::shared_ptr<lumex::node> node;
	lumex::test::system & system;
};
}

void lumex::test::system::generate_usage_traffic (uint32_t count_a, uint32_t wait_a)
{
	for (size_t i (0), n (nodes.size ()); i != n; ++i)
	{
		generate_usage_traffic (count_a, wait_a, i);
	}
}

void lumex::test::system::generate_usage_traffic (uint32_t count_a, uint32_t wait_a, size_t index_a)
{
	debug_assert (nodes.size () > index_a);
	debug_assert (count_a > 0);
	auto generate (std::make_shared<traffic_generator> (count_a, wait_a, nodes[index_a], *this));
	generate->run ();
}

void lumex::test::system::generate_rollback (lumex::node & node_a, std::vector<lumex::account> & accounts_a)
{
	auto transaction = node_a.ledger.tx_begin_write ();
	debug_assert (std::numeric_limits<CryptoPP::word32>::max () > accounts_a.size ());
	auto index (random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (accounts_a.size () - 1)));
	auto account (accounts_a[index]);
	auto info = node_a.ledger.any.account_get (transaction, account);
	if (info)
	{
		auto hash (info->open_block);
		if (hash != node_a.network_params.ledger.genesis->hash ())
		{
			accounts_a[index] = accounts_a[accounts_a.size () - 1];
			accounts_a.pop_back ();
			std::deque<std::shared_ptr<lumex::block>> rollback_list;
			auto error = node_a.ledger.rollback (transaction, hash, rollback_list);
			(void)error;
			debug_assert (!error);
			for (auto & i : rollback_list)
			{
				node_a.active.erase (*i);
			}
		}
	}
}

void lumex::test::system::generate_receive (lumex::node & node_a)
{
	std::shared_ptr<lumex::block> send_block;
	{
		auto transaction = node_a.ledger.tx_begin_read ();
		lumex::account random_account;
		random_pool::generate_block (random_account.bytes.data (), sizeof (random_account.bytes));
		auto item = node_a.ledger.any.receivable_upper_bound (transaction, random_account);
		if (item != node_a.ledger.any.receivable_end ())
		{
			send_block = node_a.ledger.any.block_get (transaction, item->first.hash);
		}
	}
	if (send_block != nullptr)
	{
		auto receive_error (wallet (0)->receive_sync (send_block, lumex::dev::genesis_key.pub, std::numeric_limits<lumex::uint128_t>::max ()));
		(void)receive_error;
	}
}

void lumex::test::system::generate_activity (lumex::node & node_a, std::vector<lumex::account> & accounts_a)
{
	auto what (random_pool::generate_byte ());
	if (what < 0x1)
	{
		logger.debug (lumex::log::type::test, "Random activity: rollback");
		generate_rollback (node_a, accounts_a);
	}
	else if (what < 0x10)
	{
		logger.debug (lumex::log::type::test, "Random activity: change known");
		generate_change_known (node_a, accounts_a);
	}
	else if (what < 0x20)
	{
		logger.debug (lumex::log::type::test, "Random activity: change unknown");
		generate_change_unknown (node_a, accounts_a);
	}
	else if (what < 0x70)
	{
		logger.debug (lumex::log::type::test, "Random activity: receive");
		generate_receive (node_a);
	}
	else if (what < 0xc0)
	{
		logger.debug (lumex::log::type::test, "Random activity: send existing");
		generate_send_existing (node_a, accounts_a);
	}
	else
	{
		logger.debug (lumex::log::type::test, "Random activity: send new");
		generate_send_new (node_a, accounts_a);
	}
}

lumex::account lumex::test::system::get_random_account (std::vector<lumex::account> & accounts_a)
{
	debug_assert (std::numeric_limits<CryptoPP::word32>::max () > accounts_a.size ());
	auto index (random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (accounts_a.size () - 1)));
	auto result (accounts_a[index]);
	return result;
}

lumex::uint128_t lumex::test::system::get_random_amount (secure::transaction const & transaction_a, lumex::node & node_a, lumex::account const & account_a)
{
	lumex::uint128_t balance = node_a.ledger.any.account_balance (transaction_a, account_a).value_or (0).number ();
	lumex::uint128_union random_amount;
	lumex::random_pool::generate_block (random_amount.bytes.data (), sizeof (random_amount.bytes));
	return (((lumex::uint256_t{ random_amount.number () } * balance) / lumex::uint256_t{ std::numeric_limits<lumex::uint128_t>::max () }).convert_to<lumex::uint128_t> ());
}

void lumex::test::system::generate_send_existing (lumex::node & node_a, std::vector<lumex::account> & accounts_a)
{
	lumex::uint128_t amount;
	lumex::account destination;
	lumex::account source;
	{
		lumex::account account;
		random_pool::generate_block (account.bytes.data (), sizeof (account.bytes));
		auto transaction = node_a.ledger.tx_begin_read ();
		auto entry = node_a.store.account.begin (transaction, account);
		if (entry == node_a.store.account.end (transaction))
		{
			entry = node_a.store.account.begin (transaction);
		}
		debug_assert (entry != node_a.store.account.end (transaction));
		destination = lumex::account (entry->first);
		source = get_random_account (accounts_a);
		amount = get_random_amount (transaction, node_a, source);
	}
	if (!amount.is_zero ())
	{
		auto hash (wallet (0)->send_sync (source, destination, amount));
		(void)hash;
		debug_assert (!hash.is_zero ());
	}
}

void lumex::test::system::generate_change_known (lumex::node & node_a, std::vector<lumex::account> & accounts_a)
{
	lumex::account source (get_random_account (accounts_a));
	if (!node_a.latest (source).is_zero ())
	{
		lumex::account destination (get_random_account (accounts_a));
		auto change_error (wallet (0)->change_sync (source, destination));
		(void)change_error;
		debug_assert (!change_error);
	}
}

void lumex::test::system::generate_change_unknown (lumex::node & node_a, std::vector<lumex::account> & accounts_a)
{
	lumex::account source (get_random_account (accounts_a));
	if (!node_a.latest (source).is_zero ())
	{
		lumex::keypair key;
		lumex::account destination (key.pub);
		auto change_error (wallet (0)->change_sync (source, destination));
		(void)change_error;
		debug_assert (!change_error);
	}
}

void lumex::test::system::generate_send_new (lumex::node & node_a, std::vector<lumex::account> & accounts_a)
{
	debug_assert (node_a.wallets.items.size () == 1);
	lumex::uint128_t amount;
	lumex::account source;
	{
		auto transaction = node_a.ledger.tx_begin_read ();
		source = get_random_account (accounts_a);
		amount = get_random_amount (transaction, node_a, source);
	}
	if (!amount.is_zero ())
	{
		auto pub_result = node_a.wallets.items.begin ()->second->deterministic_insert ();
		debug_assert (pub_result);
		accounts_a.push_back (pub_result.value ());
		auto hash = wallet (0)->send_sync (source, pub_result.value (), amount);
		(void)hash;
		debug_assert (!hash.is_zero ());
	}
}

void lumex::test::system::generate_mass_activity (uint32_t count_a, lumex::node & node_a)
{
	std::vector<lumex::account> accounts;
	auto dev_genesis_key = lumex::dev::genesis_key;
	auto insert_result = wallet (0)->insert_adhoc (dev_genesis_key.prv);
	debug_assert (insert_result);
	accounts.push_back (dev_genesis_key.pub);
	auto previous (std::chrono::steady_clock::now ());
	for (uint32_t i (0); i < count_a; ++i)
	{
		if ((i & 0xff) == 0)
		{
			auto now (std::chrono::steady_clock::now ());
			auto us (std::chrono::duration_cast<std::chrono::microseconds> (now - previous).count ());
			auto count = node_a.ledger.block_count ();
			std::cerr << boost::str (boost::format ("Mass activity iteration %1% us %2% us/t %3% block count: %4%\n") % i % us % (us / 256) % count);
			previous = now;
		}
		generate_activity (node_a, accounts);
	}
}

lumex::node_config lumex::test::system::default_config ()
{
	lumex::node_config config;
	config.peering_port = get_available_port ();
	config.representative_vote_weight_minimum = 0;
	return config;
}

uint16_t lumex::test::system::get_available_port ()
{
	auto base_port_str = std::getenv ("LUMEX_TEST_BASE_PORT");
	if (!base_port_str)
		return 0; // let the O/S decide

	// Maximum possible sockets which may feasibly be used in 1 test
	constexpr auto max = 200;
	static uint16_t current = 0;

	// Read the TEST_BASE_PORT environment and override the default base port if it exists
	uint16_t base_port = boost::lexical_cast<uint16_t> (base_port_str);

	uint16_t const available_port = base_port + current;
	++current;

	// Reset port number once we have reached the maximum
	if (current >= max)
		current = 0;

	return available_port;
}

// Makes sure everything is cleaned up
void lumex::test::cleanup_dev_directories_on_exit ()
{
	// Clean up tmp directories created by the tests. Since it's sometimes useful to
	// see log files after test failures, an environment variable is supported to
	// retain the files.
	if (std::getenv ("TEST_KEEP_TMPDIRS") == nullptr)
	{
		lumex::remove_temporary_directories ();
	}
}
