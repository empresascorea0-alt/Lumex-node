#pragma once

#include <lumex/secure/fwd.hpp>
#include <lumex/secure/receivable_iterator.hpp>

#include <optional>

namespace lumex
{
class ledger_set_cemented
{
public:
	using receivable_iterator = lumex::receivable_iterator<ledger_set_cemented>;

	explicit ledger_set_cemented (lumex::ledger const &);

public: // Operations on accounts
	std::optional<lumex::amount> account_balance (lumex::secure::transaction const &, lumex::account const &) const;
	lumex::block_hash account_head (lumex::secure::transaction const &, lumex::account const &) const;
	uint64_t account_height (lumex::secure::transaction const &, lumex::account const &) const;

public: // Operations on blocks
	std::optional<lumex::amount> block_balance (lumex::secure::transaction const &, lumex::block_hash const &) const;
	bool block_exists (lumex::secure::transaction const &, lumex::block_hash const &) const;
	bool block_exists (lumex::secure::transaction const &, lumex::block const &) const;
	bool block_exists_or_pruned (lumex::secure::transaction const &, lumex::block_hash const &) const;
	std::shared_ptr<lumex::block> block_get (lumex::secure::transaction const &, lumex::block_hash const &) const;

public: // Operations on pending entries
	receivable_iterator receivable_end () const;
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
