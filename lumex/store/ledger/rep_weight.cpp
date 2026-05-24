#include <lumex/lib/numbers.hpp>
#include <lumex/secure/parallel_traversal.hpp>
#include <lumex/store/ledger/rep_weight.hpp>

#include <boost/multiprecision/cpp_int.hpp>

#include <iostream>
#include <stdexcept>

namespace lumex::store::ledger
{
rep_weight_view::rep_weight_view (lumex::store::backend & backend_a) :
	backend{ backend_a }
{
}

uint64_t rep_weight_view::count (lumex::store::transaction const & txn) const
{
	return backend.count (txn, lumex::store::table::rep_weights);
}

lumex::uint128_t rep_weight_view::get (lumex::store::transaction const & txn, lumex::account const & representative) const
{
	lumex::store::db_val value;
	auto status = backend.get (txn, lumex::store::table::rep_weights, representative, value);
	release_assert (backend.success (status) || backend.not_found (status), backend.error_string (status));
	lumex::uint128_t weight{ 0 };
	if (backend.success (status))
	{
		lumex::uint128_union weight_union{ value };
		weight = weight_union.number ();
	}
	return weight;
}

void rep_weight_view::put (lumex::store::write_transaction const & txn, lumex::account const & representative, lumex::uint128_t const & weight)
{
	lumex::uint128_union weight_union{ weight };
	auto status = backend.put (txn, lumex::store::table::rep_weights, representative, weight_union);
	backend.release_assert_success (status);
}

void rep_weight_view::del (lumex::store::write_transaction const & txn, lumex::account const & representative)
{
	auto status = backend.del (txn, lumex::store::table::rep_weights, representative);
	backend.release_assert_success (status);
}

auto rep_weight_view::begin (lumex::store::transaction const & txn, lumex::account const & representative) const -> iterator
{
	return iterator{ backend.begin (txn, lumex::store::table::rep_weights, representative) };
}

auto rep_weight_view::begin (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.begin (txn, lumex::store::table::rep_weights) };
}

auto rep_weight_view::end (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.end (txn, lumex::store::table::rep_weights) };
}

void rep_weight_view::for_each_par (std::function<void (lumex::store::read_transaction const &, iterator, iterator)> const & action) const
{
	parallel_traversal<lumex::uint256_t> (
	[&action, this] (lumex::uint256_t const & start, lumex::uint256_t const & end, bool const is_last) {
		auto txn = this->backend.tx_begin_read ();
		action (txn, this->begin (txn, start), !is_last ? this->begin (txn, end) : this->end (txn));
	});
}
}
