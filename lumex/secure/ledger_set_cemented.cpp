#include <lumex/lib/blocks.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_set_cemented.hpp>
#include <lumex/store/ledger/account.hpp>
#include <lumex/store/ledger/block.hpp>
#include <lumex/store/ledger/confirmation_height.hpp>
#include <lumex/store/ledger/pending.hpp>
#include <lumex/store/ledger/pruned.hpp>
#include <lumex/store/ledger_store.hpp>

lumex::ledger_set_cemented::ledger_set_cemented (lumex::ledger const & ledger) :
	ledger{ ledger }
{
}

std::optional<lumex::amount> lumex::ledger_set_cemented::account_balance (secure::transaction const & transaction, lumex::account const & account_a) const
{
	auto block = block_get (transaction, account_head (transaction, account_a));
	if (!block)
	{
		return std::nullopt;
	}
	return block->balance ();
}

lumex::block_hash lumex::ledger_set_cemented::account_head (secure::transaction const & transaction, lumex::account const & account) const
{
	auto info = ledger.store.confirmation_height.get (transaction, account);
	if (!info)
	{
		return 0;
	}
	return info.value ().frontier;
}

uint64_t lumex::ledger_set_cemented::account_height (secure::transaction const & transaction, lumex::account const & account) const
{
	auto head_l = account_head (transaction, account);
	if (head_l.is_zero ())
	{
		return 0;
	}
	auto block = block_get (transaction, head_l);
	release_assert (block); // Head block must be in ledger
	return block->sideband ().height;
}

std::optional<lumex::amount> lumex::ledger_set_cemented::block_balance (secure::transaction const & transaction, lumex::block_hash const & hash) const
{
	auto block = block_get (transaction, hash);
	if (!block)
	{
		return std::nullopt;
	}
	return block->balance ();
}

bool lumex::ledger_set_cemented::block_exists (secure::transaction const & transaction, lumex::block_hash const & hash) const
{
	return block_get (transaction, hash) != nullptr;
}

bool lumex::ledger_set_cemented::block_exists (secure::transaction const & transaction, lumex::block const & block) const
{
	auto info = ledger.store.confirmation_height.get (transaction, block.account ());
	if (!info)
	{
		return false;
	}
	return block.sideband ().height <= info.value ().height;
}

bool lumex::ledger_set_cemented::block_exists_or_pruned (secure::transaction const & transaction, lumex::block_hash const & hash) const
{
	if (hash.is_zero ())
	{
		return false;
	}
	if (ledger.store.pruned.exists (transaction, hash))
	{
		return true;
	}
	return block_exists (transaction, hash);
}

std::shared_ptr<lumex::block> lumex::ledger_set_cemented::block_get (secure::transaction const & transaction, lumex::block_hash const & hash) const
{
	if (hash.is_zero ())
	{
		return nullptr;
	}
	auto block = ledger.store.block.get (transaction, hash);
	if (!block)
	{
		return nullptr;
	}
	auto info = ledger.store.confirmation_height.get (transaction, block->account ());
	if (!info)
	{
		return nullptr;
	}
	return block->sideband ().height <= info.value ().height ? block : nullptr;
}
auto lumex::ledger_set_cemented::receivable_end () const -> receivable_iterator
{
	return receivable_iterator{};
}

auto lumex::ledger_set_cemented::receivable_upper_bound (secure::transaction const & transaction, lumex::account const & account) const -> receivable_iterator
{
	return receivable_iterator{ transaction, *this, receivable_lower_bound (transaction, account.number () + 1, 0) };
}

auto lumex::ledger_set_cemented::receivable_upper_bound (secure::transaction const & transaction, lumex::account const & account, lumex::block_hash const & hash) const -> receivable_iterator
{
	auto result = receivable_lower_bound (transaction, account, hash.number () + 1);
	if (!result || result.value ().first.account != account)
	{
		return receivable_iterator{ transaction, *this, std::nullopt };
	}
	return receivable_iterator{ transaction, *this, result };
}

std::optional<std::pair<lumex::pending_key, lumex::pending_info>> lumex::ledger_set_cemented::receivable_lower_bound (secure::transaction const & transaction, lumex::account const & account, lumex::block_hash const & hash) const
{
	auto result = ledger.store.pending.begin (transaction, { account, hash });
	while (result != ledger.store.pending.end (transaction) && !block_exists (transaction, result->first.hash))
	{
		++result;
	}
	if (result == ledger.store.pending.end (transaction))
	{
		return std::nullopt;
	}
	return *result;
}
