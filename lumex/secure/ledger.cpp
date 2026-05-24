#include <lumex/crypto_lib/random_pool.hpp>
#include <lumex/lib/block_type.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/bounded_dfs.hpp>
#include <lumex/lib/files.hpp>
#include <lumex/lib/formatting.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/lib/work.hpp>
#include <lumex/node/make_store.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_processor.hpp>
#include <lumex/secure/ledger_rollback.hpp>
#include <lumex/secure/ledger_set_any.hpp>
#include <lumex/secure/ledger_set_cemented.hpp>
#include <lumex/secure/network_params.hpp>
#include <lumex/secure/rep_weights.hpp>
#include <lumex/store/ledger/account.hpp>
#include <lumex/store/ledger/block.hpp>
#include <lumex/store/ledger/confirmation_height.hpp>
#include <lumex/store/ledger/final_vote.hpp>
#include <lumex/store/ledger/online_weight.hpp>
#include <lumex/store/ledger/peer.hpp>
#include <lumex/store/ledger/pending.hpp>
#include <lumex/store/ledger/pruned.hpp>
#include <lumex/store/ledger/rep_weight.hpp>
#include <lumex/store/ledger/topology.hpp>
#include <lumex/store/ledger/version.hpp>
#include <lumex/store/ledger_store.hpp>

#include <boost/multiprecision/cpp_int.hpp>

#include <stack>

#include <cryptopp/words.h>

lumex::ledger::ledger (lumex::store::ledger_store & store_a, lumex::network_params const & params_a, lumex::stats & stats_a, lumex::logger & logger_a, ledger_options options_a) :
	constants{ params_a.ledger },
	work{ params_a.work },
	store{ store_a },
	stats{ stats_a },
	logger{ logger_a },
	options{ options_a },
	rep_weights{ store_a.rep_weight, options_a.min_rep_weight },
	max_backlog_size{ options_a.max_backlog },
	any_impl{ std::make_unique<ledger_set_any> (*this) },
	cemented_impl{ std::make_unique<ledger_set_cemented> (*this) },
	any{ *any_impl },
	cemented{ *cemented_impl }
{
	initialize ();
}

lumex::ledger::~ledger ()
{
}

auto lumex::ledger::tx_begin_write (lumex::store::writer guard_type) const -> secure::write_transaction
{
	auto guard = store.write_queue.wait (guard_type);
	auto txn = store.tx_begin_write ();
	return secure::write_transaction{ std::move (txn), std::move (guard) };
}

auto lumex::ledger::tx_begin_read () const -> secure::read_transaction
{
	return secure::read_transaction{ store.tx_begin_read () };
}

void lumex::ledger::seed_genesis (lumex::store::ledger_store & store, lumex::store::write_transaction const & txn, lumex::ledger_constants const & constants, ledger_options const & options)
{
	release_assert (store.empty (txn), "attempt to seed a non-empty ledger store");
	release_assert (constants.genesis->has_sideband ());

	store.block.put (txn, constants.genesis->hash (), *constants.genesis);

	store.confirmation_height.put (txn, constants.genesis->account (),
	lumex::confirmation_height_info{ /* height */ 1, /* frontier */ constants.genesis->hash () });

	store.account.put (txn, constants.genesis->account (),
	lumex::account_info{
	/* head */ constants.genesis->hash (),
	/* representative */ constants.genesis->account (),
	/* open_block */ constants.genesis->hash (),
	/* balance */ std::numeric_limits<lumex::uint128_t>::max (),
	/* modified */ lumex::seconds_since_epoch (),
	/* block_count */ 1,
	/* epoch */ lumex::epoch::epoch_0 });

	store.rep_weight.put (txn, constants.genesis->account (), std::numeric_limits<lumex::uint128_t>::max ());

	if (options.enable_topo_index)
	{
		store.topology.put (txn, { /* topo_height */ 1, /* hash */ constants.genesis->hash () });
		store.version.put_flag (txn, lumex::store::meta_key::topo_index_enabled, true);
	}

	store.version.put_version (txn, lumex::store::ledger_store::version_current);
}

void lumex::ledger::initialize ()
{
	debug_assert (rep_weights.empty ());

	logger.info (lumex::log::type::ledger, "Loading ledger, this may take a while...");

	bool is_initialized = false;
	{
		auto const transaction = store.tx_begin_read ();
		is_initialized = (store.account.begin (transaction) != store.account.end (transaction));
	}
	if (!is_initialized && store.get_mode () != lumex::store::open_mode::read_only)
	{
		// Store was empty meaning we just created it, seed it with genesis state
		logger.info (lumex::log::type::ledger, "Initializing ledger with genesis: {}", constants.genesis->hash ());

		auto transaction = store.tx_begin_write ();
		seed_genesis (store, transaction, constants, options);
	}

	// Load ledger flags
	{
		auto const transaction = store.tx_begin_read ();
		flags.topo_index = store.version.get_flag (transaction, lumex::store::meta_key::topo_index_enabled);

		logger.debug (lumex::log::type::ledger, "Ledger flags loaded: topo_index={}", flags.topo_index);
	}

	auto const & generate_cache_flags = options.generate_cache;

	if (generate_cache_flags.account_count || generate_cache_flags.block_count)
	{
		logger.debug (lumex::log::type::ledger, "Generating block count cache...");

		store.account.for_each_par (
		[this] (store::read_transaction const &, auto i, auto n) {
			uint64_t block_count_l{ 0 };
			uint64_t account_count_l{ 0 };
			for (; i != n; ++i)
			{
				lumex::account_info const & info (i->second);
				block_count_l += info.block_count;
				++account_count_l;
			}
			this->cache.block_count += block_count_l;
			this->cache.account_count += account_count_l;
		});

		logger.debug (lumex::log::type::ledger, "Block count cache generated");
	}

	if (generate_cache_flags.cemented_count)
	{
		logger.debug (lumex::log::type::ledger, "Generating cemented count cache...");

		store.confirmation_height.for_each_par (
		[this] (store::read_transaction const &, auto i, auto n) {
			uint64_t cemented_count_l (0);
			for (; i != n; ++i)
			{
				cemented_count_l += i->second.height;
			}
			this->cache.cemented_count += cemented_count_l;
		});

		logger.debug (lumex::log::type::ledger, "Cemented count cache generated");
	}

	{
		logger.debug (lumex::log::type::ledger, "Generating pruned count cache...");

		auto transaction = store.tx_begin_read ();
		cache.pruned_count = store.pruned.count (transaction);

		logger.debug (lumex::log::type::ledger, "Pruned count cache generated");
	}

	if (generate_cache_flags.reps)
	{
		logger.debug (lumex::log::type::ledger, "Generating representative weights cache...");

		store.rep_weight.for_each_par (
		[this] (store::read_transaction const &, auto i, auto n) {
			lumex::rep_weights rep_weights_l{ this->store.rep_weight };
			for (; i != n; ++i)
			{
				rep_weights_l.put (i->first, i->second.number ());
			}
			this->rep_weights.append_from (rep_weights_l);
		});

		store.pending.for_each_par (
		[this] (store::read_transaction const &, auto i, auto n) {
			lumex::rep_weights rep_weights_l{ this->store.rep_weight };
			for (; i != n; ++i)
			{
				rep_weights_l.put_unused (i->second.amount.number ());
			}
			this->rep_weights.append_from (rep_weights_l);
		});

		logger.debug (lumex::log::type::ledger, "Representative weights cache generated");
	}

	// Use larger precision types to detect potential overflow issues
	lumex::uint256_t active_balance, pending_balance, burned_balance;

	if (generate_cache_flags.consistency_check)
	{
		logger.debug (lumex::log::type::ledger, "Verifying ledger balance consistency...");

		// Verify sum of all account and pending balances
		lumex::locked<lumex::uint256_t> active_balance_s{ 0 };
		lumex::locked<lumex::uint256_t> pending_balance_s{ 0 };
		lumex::locked<lumex::uint256_t> burned_balance_s{ 0 };

		store.account.for_each_par (
		[&] (store::read_transaction const &, auto i, auto n) {
			lumex::uint256_t balance_l{ 0 };
			lumex::uint256_t burned_l{ 0 };
			for (; i != n; ++i)
			{
				lumex::account_info const & info = i->second;
				if (i->first == constants.burn_account)
				{
					burned_l += info.balance.number ();
				}
				else
				{
					balance_l += info.balance.number ();
				}
			}
			(*active_balance_s.lock ()) += balance_l;
			release_assert (burned_l == 0); // The burn account should not have any active balance
		});

		store.pending.for_each_par (
		[&] (store::read_transaction const &, auto i, auto n) {
			lumex::uint256_t balance_l{ 0 };
			lumex::uint256_t burned_l{ 0 };
			for (; i != n; ++i)
			{
				lumex::pending_key const & key = i->first;
				lumex::pending_info const & info = i->second;
				if (key.account == constants.burn_account)
				{
					burned_l += info.amount.number ();
				}
				else
				{
					balance_l += info.amount.number ();
				}
			}
			(*pending_balance_s.lock ()) += balance_l;
			(*burned_balance_s.lock ()) += burned_l;
		});

		active_balance = *active_balance_s.lock ();
		pending_balance = *pending_balance_s.lock ();
		burned_balance = *burned_balance_s.lock ();

		release_assert (active_balance <= std::numeric_limits<lumex::uint128_t>::max ());
		release_assert (pending_balance <= std::numeric_limits<lumex::uint128_t>::max ());
		release_assert (burned_balance <= std::numeric_limits<lumex::uint128_t>::max ());

		release_assert (active_balance + pending_balance + burned_balance == constants.genesis_amount, "ledger corruption detected: account and pending balances do not match genesis amount", to_string (active_balance) + " + " + to_string (pending_balance) + " + " + to_string (burned_balance) + " != " + to_string (constants.genesis_amount));
		release_assert (active_balance == rep_weights.get_weight_committed (), "ledger corruption detected: active balance does not match committed representative weights", to_string (active_balance) + " != " + to_string (rep_weights.get_weight_committed ()));
		release_assert (pending_balance + burned_balance == rep_weights.get_weight_unused (), "ledger corruption detected: pending balance does not match unused representative weights", to_string (pending_balance) + " != " + to_string (rep_weights.get_weight_unused ()));

		logger.debug (lumex::log::type::ledger, "Ledger balance consistency verified");
	}
	else
	{
		logger.warn (lumex::log::type::ledger, "Ledger consistency check skipped; ensure your environment provides data-integrity safeguards");
	}

	if (generate_cache_flags.reps && generate_cache_flags.consistency_check)
	{
		logger.debug (lumex::log::type::ledger, "Verifying total weights consistency...");

		rep_weights.verify_consistency (static_cast<lumex::uint128_t> (burned_balance));

		logger.debug (lumex::log::type::ledger, "Total weights consistency verified");
	}

	logger.info (lumex::log::type::ledger, "Block count:    {:>11}", cache.block_count.load ());
	logger.info (lumex::log::type::ledger, "Cemented count: {:>11}", cache.cemented_count.load ());
	logger.info (lumex::log::type::ledger, "Account count:  {:>11}", cache.account_count.load ());
	logger.info (lumex::log::type::ledger, "Pruned count:   {:>11}", cache.pruned_count.load ());
	logger.info (lumex::log::type::ledger, "Representative count: {:>5}", rep_weights.size ());
	logger.info (lumex::log::type::ledger, "Active balance: {} | pending: {} | burned: {}",
	lumex::log::as_lumex (static_cast<lumex::uint128_t> (active_balance)),
	lumex::log::as_lumex (static_cast<lumex::uint128_t> (pending_balance)),
	lumex::log::as_lumex (static_cast<lumex::uint128_t> (burned_balance)));
	logger.info (lumex::log::type::ledger, "Weight committed: {} | unused: {}",
	lumex::log::as_lumex (rep_weights.get_weight_committed ()),
	lumex::log::as_lumex (rep_weights.get_weight_unused ()));
}

void lumex::ledger::verify_consistency (secure::transaction const & transaction) const
{
	rep_weights.verify_consistency (0); // It's impractical to recompute burned weight, so we skip it here
}

bool lumex::ledger::block_uncemented (secure::transaction const & transaction, lumex::block_hash const & hash) const
{
	auto block = any.block_get (transaction, hash);
	if (block)
	{
		return !cemented.block_exists (transaction, *block);
	}
	return false; // Block doesn't exist
}

lumex::uint128_t lumex::ledger::account_receivable (secure::transaction const & transaction_a, lumex::account const & account_a, bool only_confirmed_a) const
{
	lumex::uint128_t result (0);
	lumex::account end (account_a.number () + 1);
	for (auto i (store.pending.begin (transaction_a, lumex::pending_key (account_a, 0))), n (store.pending.begin (transaction_a, lumex::pending_key (end, 0))); i != n; ++i)
	{
		lumex::pending_info const & info (i->second);
		if (only_confirmed_a)
		{
			if (cemented.block_exists_or_pruned (transaction_a, i->first.hash))
			{
				result += info.amount.number ();
			}
		}
		else
		{
			result += info.amount.number ();
		}
	}
	return result;
}

// Both stack and result set are bounded to limit maximum memory usage
// Due to the max_blocks limit, the target block may not be cemented in a single call. Callers should call this function multiple times until the target is cemented.
std::deque<std::shared_ptr<lumex::block>> lumex::ledger::cement (secure::write_transaction & transaction, lumex::block_hash const & target_hash, size_t max_blocks)
{
	std::deque<std::shared_ptr<lumex::block>> result;

	auto start_block = any.block_get (transaction, target_hash);
	release_assert (start_block, "attempting to cement a non-existent block", target_hash.to_string ());

	auto is_resolved = [&] (std::shared_ptr<lumex::block> const & block) {
		if (block)
		{
			return cemented.block_exists (transaction, *block);
		}
		return true; // Pruned, must have been cemented
	};

	auto get_dependencies = [&] (std::shared_ptr<lumex::block> const & block) {
		auto dep_hashes = block->dependencies ();
		std::array<std::shared_ptr<lumex::block>, dep_hashes.size ()> deps{};
		for (size_t i = 0; i < dep_hashes.size (); ++i)
		{
			auto const & dep_hash = dep_hashes[i];
			if (!dep_hash.is_zero ())
			{
				auto dep_block = any.block_get (transaction, dep_hash);
				if (dep_block)
				{
					deps[i] = dep_block;
				}
				else
				{
					// If the block doesn't exist, it must be pruned
					debug_assert (any.block_pruned (transaction, dep_hash), "missing dependency block", dep_hash.to_string ());
				}
				// nullptr will be filtered by is_resolved
			}
		}
		return deps;
	};

	auto resolve = [&] (std::shared_ptr<lumex::block> const & block) -> bool {
		// We must only cement blocks that have their dependencies cemented
		debug_assert (dependencies_cemented (transaction, *block));
		cement_one (transaction, *block);

		result.push_back (block);

		// Refresh the transaction to avoid long-running transactions
		// Ensure that the block wasn't rolled back during the refresh
		bool refreshed = transaction.refresh_if_needed ();
		if (refreshed)
		{
			if (!any.block_exists (transaction, target_hash))
			{
				return false; // Block was rolled back during cementing
			}
		}

		// Early return might leave parts of the dependency tree uncemented
		return result.size () < max_blocks;
	};

	// Walk the dependency tree depth-first, cementing blocks bottom-up, newly cemented blocks are collected in result
	auto dfs_result = lumex::bounded_dfs (start_block, max_blocks, is_resolved, get_dependencies, resolve);
	debug_assert (dfs_result.resolved == result.size ());

	stats.inc (lumex::stat::type::ledger, dfs_result.overflow ? lumex::stat::detail::cementing_overflow : lumex::stat::detail::cementing);
	stats.inc (lumex::stat::type::ledger, lumex::stat::detail::cemented, dfs_result.resolved);

	return result;
}

void lumex::ledger::cement_one (secure::write_transaction & transaction, lumex::block const & block)
{
	debug_assert ((!store.confirmation_height.get (transaction, block.account ()) && block.sideband ().height == 1) || store.confirmation_height.get (transaction, block.account ()).value ().height + 1 == block.sideband ().height);
	confirmation_height_info info{ block.sideband ().height, block.hash () };
	store.confirmation_height.put (transaction, block.account (), info);
	++cache.cemented_count;

	stats.inc (lumex::stat::type::confirmation_height, lumex::stat::detail::blocks_cemented);
}

lumex::block_status lumex::ledger::process (secure::write_transaction const & transaction, std::shared_ptr<lumex::block> block)
{
	debug_assert (!work.validate_entry (*block) || constants.genesis == lumex::dev::genesis);

	ledger_processor processor (transaction, *this);
	block->visit (processor);
	if (processor.result == lumex::block_status::progress)
	{
		++cache.block_count;
	}
	return processor.result;
}

namespace
{
class representative_block_visitor final : public lumex::block_visitor
{
public:
	representative_block_visitor (lumex::secure::transaction const & transaction, lumex::ledger & ledger) :
		transaction{ transaction },
		ledger{ ledger }
	{
	}

	void compute (lumex::block_hash const & hash)
	{
		current = hash;
		while (result.is_zero ())
		{
			auto block = ledger.any.block_get (transaction, current);
			release_assert (block != nullptr);
			block->visit (*this);
		}
	}

	void send_block (lumex::send_block const & block) override
	{
		current = block.previous ();
	}
	void receive_block (lumex::receive_block const & block) override
	{
		current = block.previous ();
	}
	void open_block (lumex::open_block const & block) override
	{
		result = block.hash ();
	}
	void change_block (lumex::change_block const & block) override
	{
		result = block.hash ();
	}
	void state_block (lumex::state_block const & block) override
	{
		result = block.hash ();
	}

	lumex::secure::transaction const & transaction;
	lumex::ledger & ledger;

	lumex::block_hash current{ 0 };
	lumex::block_hash result{ 0 };
};
}

lumex::block_hash lumex::ledger::representative_block (secure::transaction const & transaction, lumex::block_hash const & hash)
{
	representative_block_visitor visitor{ transaction, *this };
	visitor.compute (hash);
	auto result = visitor.result;
	debug_assert (result.is_zero () || any.block_exists (transaction, result));
	return result;
}

std::string lumex::ledger::block_text (char const * hash_a)
{
	return block_text (lumex::block_hash (hash_a));
}

std::string lumex::ledger::block_text (lumex::block_hash const & hash_a)
{
	std::string result;
	auto transaction = tx_begin_read ();
	auto block_l = any.block_get (transaction, hash_a);
	if (block_l != nullptr)
	{
		block_l->serialize_json (result);
	}
	return result;
}

std::deque<std::shared_ptr<lumex::block>> lumex::ledger::random_blocks (secure::transaction const & transaction, size_t count) const
{
	std::deque<std::shared_ptr<lumex::block>> result;

	auto const starting_hash = lumex::random_pool::generate<lumex::block_hash> ();

	// It is more efficient to choose a random starting point and pick a few sequential blocks from there
	auto it = store.block.begin (transaction, starting_hash);
	auto const end = store.block.end (transaction);
	while (result.size () < count)
	{
		if (it != end)
		{
			result.push_back (it->second.block);
		}
		++it; // Store iterators wrap around when reaching the end
	}

	return result;
}

bool lumex::ledger::bootstrap_height_reached () const
{
	return cache.block_count >= bootstrap_weights.max_blocks;
}

std::unordered_map<lumex::account, lumex::uint128_t> lumex::ledger::rep_weights_snapshot () const
{
	if (!bootstrap_height_reached ())
	{
		return bootstrap_weights.representatives;
	}
	else
	{
		return rep_weights.get_rep_amounts ();
	}
}

lumex::uint128_t lumex::ledger::weight (lumex::account const & account) const
{
	if (!bootstrap_height_reached ())
	{
		auto weight = bootstrap_weights.representatives.find (account);
		if (weight != bootstrap_weights.representatives.end ())
		{
			return weight->second;
		}
		return 0;
	}
	else
	{
		return rep_weights.get (account);
	}
}

lumex::uint128_t lumex::ledger::weight_exact (secure::transaction const & txn_a, lumex::account const & representative_a) const
{
	return store.rep_weight.get (txn_a, representative_a);
}

// Rollback blocks until `block_a' doesn't exist or it tries to penetrate the confirmation height
// TODO: Refactor rollback operation to use non-recursive algorithm
bool lumex::ledger::rollback (secure::write_transaction const & transaction_a, lumex::block_hash const & block_a, std::deque<std::shared_ptr<lumex::block>> & list_a, size_t depth, size_t const max_depth)
{
	if (depth > max_depth)
	{
		logger.critical (lumex::log::type::ledger, "Rollback depth exceeded: {} (max depth: {})", depth, max_depth);
		return true; // Error
	}

	debug_assert (any.block_exists (transaction_a, block_a));
	auto account_l = any.block_account (transaction_a, block_a).value ();
	auto block_account_height (any.block_height (transaction_a, block_a));
	ledger_rollback rollback (transaction_a, *this, list_a, depth, max_depth);
	auto error (false);
	while (!error && any.block_exists (transaction_a, block_a))
	{
		lumex::confirmation_height_info confirmation_height_info;
		store.confirmation_height.get (transaction_a, account_l, confirmation_height_info);
		if (block_account_height > confirmation_height_info.height)
		{
			auto info = any.account_get (transaction_a, account_l);
			release_assert (info);
			auto block_l = any.block_get (transaction_a, info->head);
			release_assert (block_l != nullptr);
			block_l->visit (rollback);
			error = rollback.error;
			if (!error)
			{
				list_a.push_back (block_l);
				--cache.block_count;
			}
		}
		else
		{
			error = true;
		}
	}
	return error;
}

bool lumex::ledger::rollback (secure::write_transaction const & transaction_a, lumex::block_hash const & block_a)
{
	std::deque<std::shared_ptr<lumex::block>> rollback_list;
	return rollback (transaction_a, block_a, rollback_list);
}

// Return latest root for account, account number if there are no blocks for this account.
lumex::root lumex::ledger::latest_root (secure::transaction const & transaction_a, lumex::account const & account_a)
{
	auto info = any.account_get (transaction_a, account_a);
	if (!info)
	{
		return account_a;
	}
	else
	{
		return info->head;
	}
}

bool lumex::ledger::dependencies_cemented (secure::transaction const & transaction, lumex::block const & block) const
{
	release_assert (block.has_sideband ());
	auto dependencies = block.dependencies ();
	return std::all_of (dependencies.begin (), dependencies.end (), [this, &transaction] (lumex::block_hash const & hash) {
		return hash.is_zero () || cemented.block_exists_or_pruned (transaction, hash);
	});
}

bool lumex::ledger::is_epoch_link (lumex::link const & link_a) const
{
	return constants.epochs.is_epoch_link (link_a);
}

/** Given the block hash of a send block, find the associated receive block that receives that send.
 *  The send block hash is not checked in any way, it is assumed to be correct.
 * @return Return the receive block on success and null on failure
 */
std::shared_ptr<lumex::block> lumex::ledger::find_receive_block_by_send_hash (secure::transaction const & transaction, lumex::account const & destination, lumex::block_hash const & send_block_hash)
{
	std::shared_ptr<lumex::block> result;
	debug_assert (send_block_hash != 0);

	// get the cemented frontier
	lumex::confirmation_height_info info;
	if (store.confirmation_height.get (transaction, destination, info))
	{
		return nullptr;
	}
	auto possible_receive_block = any.block_get (transaction, info.frontier);

	// walk down the chain until the source field of a receive block matches the send block hash
	while (possible_receive_block != nullptr)
	{
		if (possible_receive_block->is_receive () && send_block_hash == possible_receive_block->source ())
		{
			// we have a match
			result = possible_receive_block;
			break;
		}

		possible_receive_block = any.block_get (transaction, possible_receive_block->previous ());
	}

	return result;
}

std::shared_ptr<lumex::block> lumex::ledger::block_find (secure::transaction const & transaction, lumex::block_hash const & hash, lumex::root const & root) const
{
	// First try lookup by hash
	if (auto block = any.block_get (transaction, hash))
	{
		return block;
	}
	// If hash is not found, try lookup by root
	if (!root.is_zero ())
	{
		// Search for successor of root
		if (auto successor = any.block_successor (transaction, root.as_block_hash ()))
		{
			return any.block_get (transaction, successor.value ());
		}
		// If that fails treat root as account
		if (auto info = any.account_get (transaction, root.as_account ()))
		{
			return any.block_get (transaction, info->open_block);
		}
	}
	return nullptr;
}

std::optional<lumex::account> lumex::ledger::linked_account (secure::transaction const & transaction, lumex::block const & block)
{
	if (block.is_send ())
	{
		return block.destination ();
	}
	if (block.is_receive ())
	{
		return any.block_account (transaction, block.source ());
	}
	return std::nullopt;
}

lumex::account lumex::ledger::epoch_signer (lumex::link const & link) const
{
	return constants.epochs.signer (constants.epochs.epoch (link));
}

lumex::link lumex::ledger::epoch_link (lumex::epoch epoch) const
{
	return constants.epochs.link (epoch);
}

void lumex::ledger::update_account (secure::write_transaction const & transaction_a, lumex::account const & account_a, lumex::account_info const & old_a, lumex::account_info const & new_a)
{
	if (!new_a.head.is_zero ())
	{
		if (old_a.head.is_zero () && new_a.open_block == new_a.head)
		{
			++cache.account_count;
		}
		if (!old_a.head.is_zero () && old_a.epoch () != new_a.epoch ())
		{
			// store.account.put won't erase existing entries if they're in different tables
			store.account.del (transaction_a, account_a);
		}
		store.account.put (transaction_a, account_a, new_a);
	}
	else
	{
		debug_assert (!store.confirmation_height.exists (transaction_a, account_a));
		store.account.del (transaction_a, account_a);
		release_assert (cache.account_count > 0);
		--cache.account_count;
	}
}

std::shared_ptr<lumex::block> lumex::ledger::forked_block (secure::transaction const & transaction_a, lumex::block const & block_a)
{
	debug_assert (!any.block_exists (transaction_a, block_a.hash ()));
	auto root (block_a.root ());
	debug_assert (any.block_exists (transaction_a, root.as_block_hash ()) || store.account.exists (transaction_a, root.as_account ()));
	std::shared_ptr<lumex::block> result;
	auto successor_l = any.block_successor (transaction_a, root.as_block_hash ());
	if (successor_l)
	{
		result = any.block_get (transaction_a, successor_l.value ());
	}
	if (result == nullptr)
	{
		auto info = any.account_get (transaction_a, root.as_account ());
		release_assert (info);
		result = any.block_get (transaction_a, info->open_block);
		release_assert (result != nullptr);
	}
	return result;
}

uint64_t lumex::ledger::pruning_action (secure::write_transaction & transaction_a, lumex::block_hash const & hash_a, uint64_t const batch_size_a)
{
	uint64_t pruned_count (0);
	lumex::block_hash hash (hash_a);
	while (!hash.is_zero () && hash != constants.genesis->hash ())
	{
		auto block_l = any.block_get (transaction_a, hash);
		if (block_l != nullptr)
		{
			release_assert (cemented.block_exists (transaction_a, hash));

			store.block.del (transaction_a, hash);
			store.pruned.put (transaction_a, hash);
			if (block_l->sideband ().topo_height != 0)
			{
				store.topology.del (transaction_a, { block_l->sideband ().topo_height, hash });
			}

			hash = block_l->previous ();
			++pruned_count;
			++cache.pruned_count;

			if (pruned_count % batch_size_a == 0)
			{
				transaction_a.commit ();
				transaction_a.renew ();
			}
		}
		else if (store.pruned.exists (transaction_a, hash))
		{
			hash = 0;
		}
		else
		{
			hash = 0;
			release_assert (false, "error finding block for pruning");
		}
	}
	return pruned_count;
}

// Balance uses the maximum of current and previous block balance to avoid deprioritizing full sends
// Timestamp uses the previous block's timestamp for least-recently-used ordering within a bucket,
// falling back to the current block's sideband timestamp when there is no previous block (e.g. open blocks)
auto lumex::ledger::block_priority (lumex::secure::transaction const & transaction, lumex::block const & block) const -> block_priority_result
{
	auto const balance = block.balance ();
	auto const previous_block = !block.previous ().is_zero () ? any.block_get (transaction, block.previous ()) : nullptr;
	auto const previous_balance = previous_block ? previous_block->balance () : 0;

	// Handle full send case nicely where the balance would otherwise be 0
	auto const priority_balance = std::max (balance, block.is_send () ? previous_balance : 0);

	// Use previous block timestamp as priority timestamp for least recently used prioritization within the same bucket
	// Account info timestamp is not used here because it will get out of sync when rollbacks happen
	auto const priority_timestamp = previous_block ? previous_block->sideband ().timestamp : block.sideband ().timestamp;
	return { priority_balance, priority_timestamp };
}

lumex::epoch lumex::ledger::version (lumex::block const & block)
{
	if (block.type () == lumex::block_type::state)
	{
		return block.sideband ().details.epoch;
	}
	return lumex::epoch::epoch_0;
}

lumex::epoch lumex::ledger::version (secure::transaction const & transaction, lumex::block_hash const & hash) const
{
	auto block_l = any.block_get (transaction, hash);
	if (block_l == nullptr)
	{
		return lumex::epoch::epoch_0;
	}
	return version (*block_l);
}

uint64_t lumex::ledger::cemented_count () const
{
	return cache.cemented_count;
}

uint64_t lumex::ledger::block_count () const
{
	return cache.block_count;
}

uint64_t lumex::ledger::account_count () const
{
	return cache.account_count;
}

uint64_t lumex::ledger::pruned_count () const
{
	return cache.pruned_count;
}

uint64_t lumex::ledger::backlog_size () const
{
	auto blocks = cache.block_count.load ();
	auto cemented = cache.cemented_count.load ();
	return (blocks > cemented) ? blocks - cemented : 0;
}

uint64_t lumex::ledger::max_backlog () const
{
	auto const count = cemented_count ();
	auto const max_bootstrap_count = bootstrap_weights.max_blocks;

	if (max_backlog_size == 0)
	{
		return 0; // Unlimited backlog
	}

	// Use cemented block count to determine the switch point for backlog
	if (count >= max_bootstrap_count)
	{
		return max_backlog_size;
	}
	else
	{
		// If the bootstrap weight hasn't been reached, we allow a backlog of up to bootstrap_weights.max_blocks
		// This should avoid having to rollback too many blocks once the bootstrap weight is reached
		auto const allowed_backlog = max_bootstrap_count - count;
		return std::max (allowed_backlog, max_backlog_size);
	}
}

lumex::container_info lumex::ledger::container_info () const
{
	lumex::container_info info;
	info.put ("bootstrap_weights", bootstrap_weights.representatives);
	info.add ("rep_weights", rep_weights.container_info ());
	return info;
}
