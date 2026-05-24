#pragma once

#include <lumex/lib/config.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/timer.hpp>
#include <lumex/secure/account_info.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/fwd.hpp>
#include <lumex/secure/generate_cache_flags.hpp>
#include <lumex/secure/network_params.hpp>
#include <lumex/secure/pending_info.hpp>
#include <lumex/secure/rep_weights.hpp>
#include <lumex/secure/transaction.hpp>
#include <lumex/store/ledger_store.hpp>
#include <lumex/weights/bootstrap_weights.hpp>

#include <boost/multiprecision/cpp_int.hpp>

#include <deque>
#include <map>
#include <memory>

namespace lumex
{
class ledger;
class ledger_set_any;
class ledger_set_cemented;

class ledger_cache
{
	friend class ledger;

private:
	std::atomic<uint64_t> cemented_count{ 0 };
	std::atomic<uint64_t> block_count{ 0 };
	std::atomic<uint64_t> pruned_count{ 0 };
	std::atomic<uint64_t> account_count{ 0 };
};

struct ledger_options
{
	lumex::generate_cache_flags generate_cache{};
	lumex::uint128_t min_rep_weight{ 0 };
	uint64_t max_backlog{ 0 };
	bool enable_topo_index{ true };
};

struct ledger_flags
{
	bool topo_index{ false };
};

class ledger final
{
	template <typename T>
	friend class receivable_iterator;

public:
	ledger (lumex::store::ledger_store &, lumex::network_params const &, lumex::stats &, lumex::logger &, ledger_options = {});
	~ledger ();

	/** Start read-write transaction */
	secure::write_transaction tx_begin_write (lumex::store::writer guard_type = lumex::store::writer::generic) const;
	/** Start read-only transaction */
	secure::read_transaction tx_begin_read () const;

	lumex::uint128_t account_receivable (secure::transaction const &, lumex::account const &, bool = false) const;
	/**
	 * Returns the cached vote weight for the given representative.
	 * If the weight is below the cache limit it returns 0.
	 * During bootstrap it returns the preconfigured bootstrap weights.
	 */
	lumex::uint128_t weight (lumex::account const &) const;
	/* Returns the exact vote weight for the given representative by doing a database lookup */
	lumex::uint128_t weight_exact (secure::transaction const &, lumex::account const &) const;
	std::shared_ptr<lumex::block> forked_block (secure::transaction const &, lumex::block const &);
	lumex::root latest_root (secure::transaction const &, lumex::account const &);
	lumex::block_hash representative_block (secure::transaction const &, lumex::block_hash const &);
	std::string block_text (char const *);
	std::string block_text (lumex::block_hash const &);
	std::deque<std::shared_ptr<lumex::block>> random_blocks (secure::transaction const &, size_t count) const;
	std::optional<lumex::pending_info> pending_info (secure::transaction const &, lumex::pending_key const & key) const;
	std::deque<std::shared_ptr<lumex::block>> cement (secure::write_transaction &, lumex::block_hash const & hash, size_t max_blocks = 1024 * 128);
	lumex::block_status process (secure::write_transaction const &, std::shared_ptr<lumex::block> block);
	bool rollback (secure::write_transaction const &, lumex::block_hash const &, std::deque<std::shared_ptr<lumex::block>> & rollback_list, size_t depth = 0, size_t max_depth = lumex::ledger_max_rollback_depth ());
	bool rollback (secure::write_transaction const &, lumex::block_hash const &);
	void update_account (secure::write_transaction const &, lumex::account const &, lumex::account_info const &, lumex::account_info const &);
	uint64_t pruning_action (secure::write_transaction &, lumex::block_hash const &, uint64_t const);
	bool is_epoch_link (lumex::link const &) const;
	std::shared_ptr<lumex::block> find_receive_block_by_send_hash (secure::transaction const &, lumex::account const & destination, lumex::block_hash const & send_block_hash);
	std::optional<lumex::account> linked_account (secure::transaction const &, lumex::block const &);
	lumex::account epoch_signer (lumex::link const &) const;
	lumex::link epoch_link (lumex::epoch) const;
	bool bootstrap_height_reached () const;
	std::unordered_map<lumex::account, lumex::uint128_t> rep_weights_snapshot () const;

	static lumex::epoch version (lumex::block const & block);
	lumex::epoch version (secure::transaction const &, lumex::block_hash const & hash) const;

	/**
	 * Finds a block by hash, falling back to root-based lookups
	 * Searches: (1) direct hash lookup, (2) successor of root, (3) open block of root-as-account
	 */
	std::shared_ptr<lumex::block> block_find (secure::transaction const &, lumex::block_hash const &, lumex::root const &) const;

	/**
	 * Checks if a block exists in the ledger but has not yet been cemented
	 */
	bool block_uncemented (secure::transaction const &, lumex::block_hash const &) const;

	/**
	 * Checks if all blocks that this block depends on are cemented (or pruned)
	 */
	bool dependencies_cemented (secure::transaction const &, lumex::block const &) const;

	/**
	 * Computes the priority balance and timestamp for bucket-based prioritization
	 */
	using block_priority_result = std::pair<lumex::amount, lumex::priority_timestamp>;
	block_priority_result block_priority (secure::transaction const &, lumex::block const &) const;

	uint64_t cemented_count () const;
	uint64_t block_count () const;
	uint64_t account_count () const;
	uint64_t pruned_count () const;
	uint64_t backlog_size () const;
	uint64_t max_backlog () const;

	void verify_consistency (secure::transaction const &) const;

	/**
	 * Walk every block in the ledger, compute and persist its topology height, then enable the topology index flag
	 * Intended as a one-time offline upgrade for ledgers initialized before the topology index existed
	 */
	void populate_topo_index ();

	/**
	 * Drop the topology index table and disable the topology index flag.
	 * Intended for users who need to enable pruning, which is incompatible with the topology index.
	 */
	void drop_topo_index ();

	lumex::container_info container_info () const;

public:
	static lumex::uint128_t const unit;

	lumex::ledger_constants const & constants;
	lumex::work_thresholds const & work;
	lumex::store::ledger_store & store;
	lumex::stats & stats;
	lumex::logger & logger;

public:
	lumex::ledger_options const options;
	lumex::ledger_flags flags;
	lumex::ledger_cache cache;
	lumex::rep_weights rep_weights;

public:
	uint64_t const max_backlog_size{ 0 };
	bool pruning{ false };

	lumex::bootstrap_weights bootstrap_weights{};

public:
	/**
	 * Seed a fresh ledger store with genesis state. Aborts if the store is not empty.
	 */
	static void seed_genesis (lumex::store::ledger_store &, lumex::store::write_transaction const &, lumex::ledger_constants const &, ledger_options const & = {});

private:
	void initialize ();
	void cement_one (secure::write_transaction &, lumex::block const & block);

	std::unique_ptr<ledger_set_any> any_impl;
	std::unique_ptr<ledger_set_cemented> cemented_impl;

public:
	ledger_set_any & any;
	ledger_set_cemented & cemented;
};
}
