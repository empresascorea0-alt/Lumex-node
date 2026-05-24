#include <lumex/secure/parallel_traversal.hpp>
#include <lumex/store/ledger/successor.hpp>

namespace lumex::store::ledger
{
successor_view::successor_view (lumex::store::backend & backend_a) :
	backend{ backend_a }
{
}

void successor_view::put (lumex::store::write_transaction const & txn, lumex::block_hash const & hash, lumex::block_hash const & successor)
{
	auto status = backend.put (txn, lumex::store::table::successor, hash, successor);
	backend.release_assert_success (status);
}

void successor_view::del (lumex::store::write_transaction const & txn, lumex::block_hash const & hash)
{
	auto status = backend.del (txn, lumex::store::table::successor, hash);
	backend.release_assert_success (status);
}

std::optional<lumex::block_hash> successor_view::get (lumex::store::transaction const & txn, lumex::block_hash const & hash) const
{
	lumex::store::db_val value;
	auto status = backend.get (txn, lumex::store::table::successor, hash, value);
	if (backend.success (status))
	{
		return lumex::block_hash{ value };
	}
	return std::nullopt;
}

bool successor_view::exists (lumex::store::transaction const & txn, lumex::block_hash const & hash) const
{
	return backend.exists (txn, lumex::store::table::successor, hash);
}

size_t successor_view::count (lumex::store::transaction const & txn) const
{
	return backend.count (txn, lumex::store::table::successor);
}

void successor_view::clear ()
{
	auto status = backend.clear (lumex::store::table::successor);
	backend.release_assert_success (status);
}

auto successor_view::begin (lumex::store::transaction const & txn, lumex::block_hash const & hash) const -> iterator
{
	return iterator{ backend.begin (txn, lumex::store::table::successor, hash) };
}

auto successor_view::begin (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.begin (txn, lumex::store::table::successor) };
}

auto successor_view::end (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.end (txn, lumex::store::table::successor) };
}

void successor_view::for_each_par (std::function<void (lumex::store::read_transaction const &, iterator, iterator)> const & action) const
{
	parallel_traversal<lumex::uint256_t> (
	[&action, this] (lumex::uint256_t const & start, lumex::uint256_t const & end, bool const is_last) {
		auto txn = this->backend.tx_begin_read ();
		action (txn, this->begin (txn, start), !is_last ? this->begin (txn, end) : this->end (txn));
	});
}
}
