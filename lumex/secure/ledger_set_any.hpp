#pragma once

#include <lumex/secure/account_iterator.hpp>
#include <lumex/secure/fwd.hpp>
#include <lumex/secure/receivable_iterator.hpp>

#include <optional>

namespace lumex
{
class ledger_set_any
{
public:
	using account_iterator = lumex::account_iterator<ledger_set_any>;
	using receivable_iterator = lumex::receivable_iterator<ledger_set_any>;

	explicit ledger_set_any (lumex::ledger const &);

public: // Operations on accounts
	std::optional<lumex::amount> account_balance (lumex::secure::transaction const &, lumex::account const &) const;
	account_iterator account_begin (lumex::secure::transaction const &) const;
	account_iterator account_end () const;
	std::optional<lumex::account_info> account_get (lumex::secure::transaction const &, lumex::account const &) const;
	bool account_exists (lumex::secure::transaction const &, lumex::account const &) const;
	lumex::block_hash account_head (lumex::secure::transaction const &, lumex::account const &) const;
	uint64_t account_height (lumex::secure::transaction const &, lumex::account const &) const;
	// Returns the next account entry equal or greater than 'account'
	// Mirrors std::map::lower_bound
	account_iterator account_lower_bound (lumex::secure::transaction const &, lumex::account const &) const;
	// Returns the next account entry greater than 'account'
	// Returns account_lower_bound (transaction, account + 1)
	// Mirrors std::map::upper_bound
	account_iterator account_upper_bound (lumex::secure::transaction const &, lumex::account const &) const;

public: // Operations on blocks
	std::optional<lumex::account> block_account (lumex::secure::transaction const &, lumex::block_hash const &) const;
	std::optional<lumex::amount> block_amount (lumex::secure::transaction const &, lumex::block_hash const &) const;
	std::optional<lumex::amount> block_amount (lumex::secure::transaction const &, std::shared_ptr<lumex::block> const &) const;
	std::optional<lumex::amount> block_balance (lumex::secure::transaction const &, lumex::block_hash const &) const;
	bool block_exists (lumex::secure::transaction const &, lumex::block_hash const &) const;
	bool block_exists_or_pruned (lumex::secure::transaction const &, lumex::block_hash const &) const;
	std::shared_ptr<lumex::block> block_get (lumex::secure::transaction const &, lumex::block_hash const &) const;
	bool block_pruned (lumex::secure::transaction const &, lumex::block_hash const &) const;
	uint64_t block_height (lumex::secure::transaction const &, lumex::block_hash const &) const;
	std::optional<lumex::block_hash> block_successor (lumex::secure::transaction const &, lumex::block_hash const &) const;
	std::optional<lumex::block_hash> block_successor (lumex::secure::transaction const &, lumex::qualified_root const &) const;

public: // Operations on pending entries
	std::optional<lumex::pending_info> pending_get (lumex::secure::transaction const &, lumex::pending_key const &) const;
	receivable_iterator receivable_end () const;
	bool receivable_exists (lumex::secure::transaction const &, lumex::account const &) const;
	// Returns the next receivable entry equal or greater than 'key'
	// Mirrors std::map::lower_bound
	std::optional<std::pair<lumex::pending_key, lumex::pending_info>> receivable_lower_bound (lumex::secure::transaction const &, lumex::account const &, lumex::block_hash const &) const;
	// Returns the next receivable entry for an account greater than 'account'
	// Returns receivable_lower_bound (transaction, account + 1, 0)
	// Mirrors std::map::upper_bound
	receivable_iterator receivable_upper_bound (lumex::secure::transaction const &, lumex::account const &) const;
	// Returns the next receivable entry for the account 'account' with hash greater than 'hash'
	// Returns receivable_lower_bound (transaction, account + 1, hash)
	// Mirrors std::map::upper_bound
	receivable_iterator receivable_upper_bound (lumex::secure::transaction const &, lumex::account const &, lumex::block_hash const &) const;

private:
	lumex::ledger const & ledger;
};
}
