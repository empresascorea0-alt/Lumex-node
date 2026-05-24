#include <lumex/lib/blockbuilders.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/files.hpp>
#include <lumex/node/make_store.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/test_common/ledger_context.hpp>

lumex::test::ledger_context::ledger_context (std::deque<std::shared_ptr<lumex::block>> && blocks) :
	store_m{ lumex::make_store (logger_m, stats_m, lumex::unique_path (), lumex::dev::constants, false, true, lumex::node_config{}) },
	ledger_m{ *store_m, lumex::dev::network_params, stats_m, logger_m },
	blocks_m{ blocks },
	pool_m{ lumex::dev::network_params.network, 1 }
{
	auto tx = ledger_m.tx_begin_write ();
	for (auto const & i : blocks_m)
	{
		auto process_result = ledger_m.process (tx, i);
		debug_assert (process_result == lumex::block_status::progress, to_string (process_result));
	}
}

lumex::ledger & lumex::test::ledger_context::ledger ()
{
	return ledger_m;
}

lumex::store::ledger_store & lumex::test::ledger_context::store ()
{
	return *store_m;
}

lumex::stats & lumex::test::ledger_context::stats ()
{
	return stats_m;
}

lumex::logger & lumex::test::ledger_context::logger ()
{
	return logger_m;
}

std::deque<std::shared_ptr<lumex::block>> const & lumex::test::ledger_context::blocks () const
{
	return blocks_m;
}

lumex::work_pool & lumex::test::ledger_context::pool ()
{
	return pool_m;
}

/*
 * Ledger facotries
 */

auto lumex::test::ledger_empty () -> ledger_context
{
	return ledger_context{};
}

auto lumex::test::ledger_send_receive () -> ledger_context
{
	std::deque<std::shared_ptr<lumex::block>> blocks;
	lumex::work_pool pool{ lumex::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	lumex::block_builder builder;
	auto send = builder.state ()
				.make_block ()
				.account (lumex::dev::genesis_key.pub)
				.previous (lumex::dev::genesis->hash ())
				.representative (lumex::dev::genesis_key.pub)
				.balance (lumex::dev::constants.genesis_amount - 1)
				.link (lumex::dev::genesis_key.pub)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*pool.generate (lumex::dev::genesis->hash ()))
				.build ();
	blocks.push_back (send);
	auto receive = builder.state ()
				   .make_block ()
				   .account (lumex::dev::genesis_key.pub)
				   .previous (send->hash ())
				   .representative (lumex::dev::genesis_key.pub)
				   .balance (lumex::dev::constants.genesis_amount)
				   .link (send->hash ())
				   .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				   .work (*pool.generate (send->hash ()))
				   .build ();
	blocks.push_back (receive);
	return ledger_context{ std::move (blocks) };
}

auto lumex::test::ledger_send_receive_legacy () -> ledger_context
{
	std::deque<std::shared_ptr<lumex::block>> blocks;
	lumex::work_pool pool{ lumex::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	lumex::block_builder builder;
	auto send = builder.send ()
				.make_block ()
				.previous (lumex::dev::genesis->hash ())
				.destination (lumex::dev::genesis_key.pub)
				.balance (lumex::dev::constants.genesis_amount - 1)
				.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				.work (*pool.generate (lumex::dev::genesis->hash ()))
				.build ();
	blocks.push_back (send);
	auto receive = builder.receive ()
				   .make_block ()
				   .previous (send->hash ())
				   .source (send->hash ())
				   .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
				   .work (*pool.generate (send->hash ()))
				   .build ();
	blocks.push_back (receive);
	return ledger_context{ std::move (blocks) };
}

auto lumex::test::ledger_diamond (unsigned height) -> ledger_context
{
	std::deque<std::shared_ptr<lumex::block>> blocks;
	lumex::work_pool pool{ lumex::dev::network_params.network, std::numeric_limits<unsigned>::max () };

	using account_block_pair = std::pair<lumex::keypair, std::shared_ptr<lumex::block>>;
	std::deque<account_block_pair> previous;
	previous.push_back ({ lumex::dev::genesis_key, lumex::dev::genesis });

	// Expanding tree
	for (unsigned level = 0; level < height; ++level)
	{
		std::deque<account_block_pair> current;
		while (!previous.empty ())
		{
			auto const [key, root] = previous.front ();
			previous.pop_front ();

			auto balance = root->balance_field ().value_or (lumex::dev::constants.genesis_amount);

			lumex::keypair target1, target2;
			lumex::block_builder builder;

			auto send1 = builder.state ()
						 .make_block ()
						 .account (key.pub)
						 .previous (root->hash ())
						 .representative (lumex::dev::genesis_key.pub)
						 .balance (balance.number () / 2)
						 .link (target1.pub)
						 .sign (key.prv, key.pub)
						 .work (*pool.generate (root->hash ()))
						 .build ();

			auto send2 = builder.state ()
						 .make_block ()
						 .account (key.pub)
						 .previous (send1->hash ())
						 .representative (lumex::dev::genesis_key.pub)
						 .balance (0)
						 .link (target2.pub)
						 .sign (key.prv, key.pub)
						 .work (*pool.generate (send1->hash ()))
						 .build ();

			auto open1 = builder.state ()
						 .make_block ()
						 .account (target1.pub)
						 .previous (0)
						 .representative (lumex::dev::genesis_key.pub)
						 .balance (balance.number () - balance.number () / 2)
						 .link (send1->hash ())
						 .sign (target1.prv, target1.pub)
						 .work (*pool.generate (target1.pub))
						 .build ();

			auto open2 = builder.state ()
						 .make_block ()
						 .account (target2.pub)
						 .previous (0)
						 .representative (lumex::dev::genesis_key.pub)
						 .balance (balance.number () / 2)
						 .link (send2->hash ())
						 .sign (target2.prv, target2.pub)
						 .work (*pool.generate (target2.pub))
						 .build ();

			blocks.push_back (send1);
			blocks.push_back (send2);
			blocks.push_back (open1);
			blocks.push_back (open2);

			current.push_back ({ target1, open1 });
			current.push_back ({ target2, open2 });
		}
		previous.clear ();
		previous.swap (current);
	}

	// Contracting tree
	while (previous.size () > 1)
	{
		std::deque<account_block_pair> current;
		while (!previous.empty ())
		{
			auto const [key1, root1] = previous.front ();
			previous.pop_front ();
			auto const [key2, root2] = previous.front ();
			previous.pop_front ();

			lumex::keypair target;
			lumex::block_builder builder;

			auto balance1 = root1->balance_field ().value ().number ();
			auto balance2 = root2->balance_field ().value ().number ();

			auto send1 = builder.state ()
						 .make_block ()
						 .account (key1.pub)
						 .previous (root1->hash ())
						 .representative (lumex::dev::genesis_key.pub)
						 .balance (0)
						 .link (target.pub)
						 .sign (key1.prv, key1.pub)
						 .work (*pool.generate (root1->hash ()))
						 .build ();

			auto send2 = builder.state ()
						 .make_block ()
						 .account (key2.pub)
						 .previous (root2->hash ())
						 .representative (lumex::dev::genesis_key.pub)
						 .balance (0)
						 .link (target.pub)
						 .sign (key2.prv, key2.pub)
						 .work (*pool.generate (root2->hash ()))
						 .build ();

			auto receive1 = builder.state ()
							.make_block ()
							.account (target.pub)
							.previous (0)
							.representative (lumex::dev::genesis_key.pub)
							.balance (balance1)
							.link (send1->hash ())
							.sign (target.prv, target.pub)
							.work (*pool.generate (target.pub))
							.build ();

			auto receive2 = builder.state ()
							.make_block ()
							.account (target.pub)
							.previous (receive1->hash ())
							.representative (lumex::dev::genesis_key.pub)
							.balance (balance1 + balance2)
							.link (send2->hash ())
							.sign (target.prv, target.pub)
							.work (*pool.generate (receive1->hash ()))
							.build ();

			blocks.push_back (send1);
			blocks.push_back (send2);
			blocks.push_back (receive1);
			blocks.push_back (receive2);

			current.push_back ({ target, receive2 });
		}
		previous.clear ();
		previous.swap (current);
	}

	return ledger_context{ std::move (blocks) };
}

auto lumex::test::ledger_single_chain (unsigned height) -> lumex::test::ledger_context
{
	std::deque<std::shared_ptr<lumex::block>> blocks;
	lumex::work_pool pool{ lumex::dev::network_params.network, std::numeric_limits<unsigned>::max () };

	lumex::block_builder builder;
	auto previous = lumex::dev::genesis;
	for (unsigned i = 0; i < height / 4; ++i)
	{
		auto send1 = builder.state ()
					 .make_block ()
					 .account (lumex::dev::genesis_key.pub)
					 .previous (previous->hash ())
					 .representative (lumex::dev::genesis_key.pub)
					 .balance (lumex::dev::constants.genesis_amount - 1)
					 .link (lumex::dev::genesis_key.pub)
					 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					 .work (*pool.generate (previous->hash ()))
					 .build ();

		auto send2 = builder.state ()
					 .make_block ()
					 .account (lumex::dev::genesis_key.pub)
					 .previous (send1->hash ())
					 .representative (lumex::dev::genesis_key.pub)
					 .balance (lumex::dev::constants.genesis_amount - 2)
					 .link (lumex::dev::genesis_key.pub)
					 .sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
					 .work (*pool.generate (send1->hash ()))
					 .build ();

		auto receive1 = builder.state ()
						.make_block ()
						.account (lumex::dev::genesis_key.pub)
						.previous (send2->hash ())
						.representative (lumex::dev::genesis_key.pub)
						.balance (lumex::dev::constants.genesis_amount - 1)
						.link (send1->hash ())
						.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
						.work (*pool.generate (send2->hash ()))
						.build ();

		auto receive2 = builder.state ()
						.make_block ()
						.account (lumex::dev::genesis_key.pub)
						.previous (receive1->hash ())
						.representative (lumex::dev::genesis_key.pub)
						.balance (lumex::dev::constants.genesis_amount)
						.link (send2->hash ())
						.sign (lumex::dev::genesis_key.prv, lumex::dev::genesis_key.pub)
						.work (*pool.generate (receive1->hash ()))
						.build ();

		blocks.push_back (send1);
		blocks.push_back (send2);
		blocks.push_back (receive1);
		blocks.push_back (receive2);

		previous = receive2;
	}

	return ledger_context{ std::move (blocks) };
}