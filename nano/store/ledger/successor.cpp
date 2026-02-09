#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/ledger/successor.hpp>

namespace nano::store::ledger
{
successor_view::successor_view (nano::store::backend & backend_a) :
	backend{ backend_a }
{
}

void successor_view::put (nano::store::write_transaction const & txn, nano::block_hash const & hash, nano::block_hash const & successor)
{
	auto status = backend.put (txn, nano::store::table::successor, hash, successor);
	backend.release_assert_success (status);
}

void successor_view::del (nano::store::write_transaction const & txn, nano::block_hash const & hash)
{
	auto status = backend.del (txn, nano::store::table::successor, hash);
	backend.release_assert_success (status);
}

std::optional<nano::block_hash> successor_view::get (nano::store::transaction const & txn, nano::block_hash const & hash) const
{
	nano::store::db_val value;
	auto status = backend.get (txn, nano::store::table::successor, hash, value);
	if (backend.success (status))
	{
		return nano::block_hash{ value };
	}
	return std::nullopt;
}

bool successor_view::exists (nano::store::transaction const & txn, nano::block_hash const & hash) const
{
	return backend.exists (txn, nano::store::table::successor, hash);
}

size_t successor_view::count (nano::store::transaction const & txn) const
{
	return backend.count (txn, nano::store::table::successor);
}

void successor_view::clear ()
{
	auto status = backend.clear (nano::store::table::successor);
	backend.release_assert_success (status);
}

auto successor_view::begin (nano::store::transaction const & txn, nano::block_hash const & hash) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::successor, hash) };
}

auto successor_view::begin (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::successor) };
}

auto successor_view::end (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.end (txn, nano::store::table::successor) };
}

void successor_view::for_each_par (std::function<void (nano::store::read_transaction const &, iterator, iterator)> const & action) const
{
	parallel_traversal<nano::uint256_t> (
	[&action, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
		auto txn = this->backend.tx_begin_read ();
		action (txn, this->begin (txn, start), !is_last ? this->begin (txn, end) : this->end (txn));
	});
}
}
