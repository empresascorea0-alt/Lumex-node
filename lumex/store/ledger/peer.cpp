#include <lumex/store/ledger/peer.hpp>

namespace lumex::store::ledger
{
peer_view::peer_view (lumex::store::backend & backend_a) :
	backend{ backend_a }
{
}

void peer_view::put (lumex::store::write_transaction const & txn, lumex::endpoint_key const & endpoint, lumex::millis_t timestamp)
{
	auto status = backend.put (txn, lumex::store::table::peers, endpoint, timestamp);
	backend.release_assert_success (status);
}

lumex::millis_t peer_view::get (lumex::store::transaction const & txn, lumex::endpoint_key const & endpoint) const
{
	lumex::millis_t result{ 0 };
	lumex::store::db_val value;
	auto status = backend.get (txn, lumex::store::table::peers, endpoint, value);
	release_assert (backend.success (status) || backend.not_found (status), backend.error_string (status));
	if (backend.success (status) && value.size () > 0)
	{
		result = static_cast<lumex::millis_t> (value);
	}
	return result;
}

void peer_view::del (lumex::store::write_transaction const & txn, lumex::endpoint_key const & endpoint)
{
	auto status = backend.del (txn, lumex::store::table::peers, endpoint);
	backend.release_assert_success (status);
}

bool peer_view::exists (lumex::store::transaction const & txn, lumex::endpoint_key const & endpoint) const
{
	return backend.exists (txn, lumex::store::table::peers, endpoint);
}

size_t peer_view::count (lumex::store::transaction const & txn) const
{
	return backend.count (txn, lumex::store::table::peers);
}

void peer_view::clear ()
{
	auto status = backend.clear (lumex::store::table::peers);
	backend.release_assert_success (status);
}

auto peer_view::begin (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.begin (txn, lumex::store::table::peers) };
}

auto peer_view::end (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.end (txn, lumex::store::table::peers) };
}
}
