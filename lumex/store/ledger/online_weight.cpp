#include <lumex/store/ledger/online_weight.hpp>

namespace lumex::store::ledger
{
online_weight_view::online_weight_view (lumex::store::backend & backend_a) :
	backend{ backend_a }
{
}

void online_weight_view::put (lumex::store::write_transaction const & txn, uint64_t time, lumex::amount const & amount)
{
	auto status = backend.put (txn, lumex::store::table::online_weight, time, amount);
	backend.release_assert_success (status);
}

void online_weight_view::del (lumex::store::write_transaction const & txn, uint64_t time)
{
	auto status = backend.del (txn, lumex::store::table::online_weight, time);
	backend.release_assert_success (status);
}

auto online_weight_view::begin (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.begin (txn, lumex::store::table::online_weight) };
}

auto online_weight_view::end (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.end (txn, lumex::store::table::online_weight) };
}

auto online_weight_view::rbegin (lumex::store::transaction const & txn) const -> reverse_iterator
{
	return reverse_iterator{ std::prev (end (txn)) };
}

auto online_weight_view::rend (lumex::store::transaction const & txn) const -> reverse_iterator
{
	return reverse_iterator{ end (txn) };
}

size_t online_weight_view::count (lumex::store::transaction const & txn) const
{
	return backend.count (txn, lumex::store::table::online_weight);
}

void online_weight_view::clear ()
{
	auto status = backend.clear (lumex::store::table::online_weight);
	backend.release_assert_success (status);
}
}
