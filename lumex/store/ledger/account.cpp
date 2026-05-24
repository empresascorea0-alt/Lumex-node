#include <lumex/secure/account_info.hpp>
#include <lumex/secure/parallel_traversal.hpp>
#include <lumex/store/db_val.hpp>
#include <lumex/store/ledger/account.hpp>

namespace lumex::store::ledger
{
account_view::account_view (lumex::store::backend & backend_a) :
	backend{ backend_a }
{
}

void account_view::put (lumex::store::write_transaction const & txn, lumex::account const & account, lumex::account_info const & info)
{
	auto status = backend.put (txn, lumex::store::table::accounts, account, info);
	backend.release_assert_success (status);
}

bool account_view::get (lumex::store::transaction const & txn, lumex::account const & account, lumex::account_info & info) const
{
	lumex::store::db_val value;
	auto status = backend.get (txn, lumex::store::table::accounts, account, value);
	release_assert (backend.success (status) || backend.not_found (status), backend.error_string (status));
	bool result = true;
	if (backend.success (status))
	{
		// TODO: Use db_val serialization operators
		lumex::bufferstream stream{ reinterpret_cast<uint8_t const *> (value.data ()), value.size () };
		result = info.deserialize (stream);
	}
	return result;
}

std::optional<lumex::account_info> account_view::get (lumex::store::transaction const & txn, lumex::account const & account) const
{
	lumex::account_info info;
	bool error = get (txn, account, info);
	if (error)
	{
		return std::nullopt;
	}
	return info;
}

void account_view::del (lumex::store::write_transaction const & txn, lumex::account const & account)
{
	auto status = backend.del (txn, lumex::store::table::accounts, account);
	backend.release_assert_success (status);
}

bool account_view::exists (lumex::store::transaction const & txn, lumex::account const & account) const
{
	return backend.exists (txn, lumex::store::table::accounts, account);
}

size_t account_view::count (lumex::store::transaction const & txn) const
{
	return backend.count (txn, lumex::store::table::accounts);
}

auto account_view::begin (lumex::store::transaction const & txn, lumex::account const & account) const -> iterator
{
	return iterator{ backend.begin (txn, lumex::store::table::accounts, account) };
}

auto account_view::begin (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.begin (txn, lumex::store::table::accounts) };
}

auto account_view::rbegin (lumex::store::transaction const & txn) const -> reverse_iterator
{
	return reverse_iterator{ std::prev (end (txn)) };
}

auto account_view::rend (lumex::store::transaction const & txn) const -> reverse_iterator
{
	return reverse_iterator{ end (txn) };
}

auto account_view::end (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.end (txn, lumex::store::table::accounts) };
}

void account_view::for_each_par (std::function<void (lumex::store::read_transaction const &, iterator, iterator)> const & action) const
{
	parallel_traversal<lumex::uint256_t> (
	[&action, this] (lumex::uint256_t const & start, lumex::uint256_t const & end, bool const is_last) {
		auto transaction = this->backend.tx_begin_read ();
		action (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end (transaction));
	});
}
}