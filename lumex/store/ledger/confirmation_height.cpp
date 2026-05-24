#include <lumex/secure/parallel_traversal.hpp>
#include <lumex/store/ledger/confirmation_height.hpp>

namespace lumex::store::ledger
{
confirmation_height_view::confirmation_height_view (lumex::store::backend & backend_a) :
	backend{ backend_a }
{
}

void confirmation_height_view::put (lumex::store::write_transaction const & txn, lumex::account const & account, lumex::confirmation_height_info const & confirmation_height_info)
{
	auto status = backend.put (txn, lumex::store::table::confirmation_height, account, confirmation_height_info);
	backend.release_assert_success (status);
}

bool confirmation_height_view::get (lumex::store::transaction const & txn, lumex::account const & account, lumex::confirmation_height_info & confirmation_height_info) const
{
	lumex::store::db_val value;
	auto status = backend.get (txn, lumex::store::table::confirmation_height, account, value);
	release_assert (backend.success (status) || backend.not_found (status), backend.error_string (status));
	bool result = true;
	if (backend.success (status))
	{
		// TODO: Use db_val serialization operators
		lumex::bufferstream stream{ reinterpret_cast<uint8_t const *> (value.data ()), value.size () };
		result = confirmation_height_info.deserialize (stream);
	}
	if (result)
	{
		confirmation_height_info.height = 0;
		confirmation_height_info.frontier = 0;
	}
	return result;
}

std::optional<lumex::confirmation_height_info> confirmation_height_view::get (lumex::store::transaction const & txn, lumex::account const & account)
{
	lumex::confirmation_height_info info;
	bool error = get (txn, account, info);
	if (error)
	{
		return std::nullopt;
	}
	return info;
}

bool confirmation_height_view::exists (lumex::store::transaction const & txn, lumex::account const & account) const
{
	return backend.exists (txn, lumex::store::table::confirmation_height, account);
}

void confirmation_height_view::del (lumex::store::write_transaction const & txn, lumex::account const & account)
{
	auto status = backend.del (txn, lumex::store::table::confirmation_height, account);
	backend.release_assert_success (status);
}

uint64_t confirmation_height_view::count (lumex::store::transaction const & txn) const
{
	return backend.count (txn, lumex::store::table::confirmation_height);
}

bool confirmation_height_view::empty (lumex::store::transaction const & txn) const
{
	return backend.empty (txn, lumex::store::table::confirmation_height);
}

void confirmation_height_view::clear ()
{
	backend.clear (lumex::store::table::confirmation_height);
}

auto confirmation_height_view::begin (lumex::store::transaction const & txn, lumex::account const & account) const -> iterator
{
	return iterator{ backend.begin (txn, lumex::store::table::confirmation_height, account) };
}

auto confirmation_height_view::begin (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.begin (txn, lumex::store::table::confirmation_height) };
}

auto confirmation_height_view::end (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.end (txn, lumex::store::table::confirmation_height) };
}

void confirmation_height_view::for_each_par (std::function<void (lumex::store::read_transaction const &, iterator, iterator)> const & action) const
{
	parallel_traversal<lumex::uint256_t> (
	[&action, this] (lumex::uint256_t const & start, lumex::uint256_t const & end, bool const is_last) {
		auto txn = this->backend.tx_begin_read ();
		action (txn, this->begin (txn, start), !is_last ? this->begin (txn, end) : this->end (txn));
	});
}
}
