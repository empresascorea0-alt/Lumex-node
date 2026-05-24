#include <lumex/store/backend.hpp>
#include <lumex/store/ledger/topology.hpp>

namespace lumex::store::ledger
{
topology_view::topology_view (lumex::store::backend & backend_a) :
	backend{ backend_a }
{
}

void topology_view::put (lumex::store::write_transaction const & txn, lumex::topo_key const & key)
{
	auto status = backend.put (txn, lumex::store::table::topology, key, nullptr);
	backend.release_assert_success (status);
}

void topology_view::del (lumex::store::write_transaction const & txn, lumex::topo_key const & key)
{
	auto status = backend.del (txn, lumex::store::table::topology, key);
	backend.release_assert_success (status);
}

bool topology_view::exists (lumex::store::transaction const & txn, lumex::topo_key const & key) const
{
	return backend.exists (txn, lumex::store::table::topology, key);
}

std::optional<uint64_t> topology_view::latest (lumex::store::transaction const & txn) const
{
	auto first = begin (txn);
	auto i = end (txn);
	if (i == first)
	{
		return std::nullopt;
	}
	--i;
	return i->first.topo_height;
}

uint64_t topology_view::count (lumex::store::transaction const & txn) const
{
	return backend.count (txn, lumex::store::table::topology);
}

void topology_view::clear ()
{
	auto status = backend.clear (lumex::store::table::topology);
	backend.release_assert_success (status);
}

auto topology_view::begin (lumex::store::transaction const & txn, lumex::topo_key const & key) const -> iterator
{
	return iterator{ backend.begin (txn, lumex::store::table::topology, key) };
}

auto topology_view::begin (lumex::store::transaction const & txn, uint64_t topo_height) const -> iterator
{
	return iterator{ backend.begin (txn, lumex::store::table::topology, lumex::topo_key{ topo_height, 0 }) };
}

auto topology_view::begin (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.begin (txn, lumex::store::table::topology) };
}

auto topology_view::end (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.end (txn, lumex::store::table::topology) };
}
}
