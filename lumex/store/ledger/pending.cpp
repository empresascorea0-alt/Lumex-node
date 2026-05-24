#include <lumex/secure/parallel_traversal.hpp>
#include <lumex/store/ledger/pending.hpp>

#include <boost/multiprecision/cpp_int.hpp>

namespace lumex::store::ledger
{
pending_view::pending_view (lumex::store::backend & backend_a) :
	backend{ backend_a }
{
}

void pending_view::put (lumex::store::write_transaction const & txn, lumex::pending_key const & key, lumex::pending_info const & pending)
{
	auto status = backend.put (txn, lumex::store::table::pending, key, pending);
	backend.release_assert_success (status);
}

void pending_view::del (lumex::store::write_transaction const & txn, lumex::pending_key const & key)
{
	auto status = backend.del (txn, lumex::store::table::pending, key);
	backend.release_assert_success (status);
}

auto pending_view::get (lumex::store::transaction const & txn, lumex::pending_key const & key) const -> std::optional<lumex::pending_info>
{
	lumex::store::db_val value;
	auto status = backend.get (txn, lumex::store::table::pending, key, value);
	release_assert (backend.success (status) || backend.not_found (status), backend.error_string (status));
	std::optional<lumex::pending_info> result;
	if (backend.success (status))
	{
		// TODO: Use db_val serialization operators
		lumex::bufferstream stream{ reinterpret_cast<uint8_t const *> (value.data ()), value.size () };
		result = lumex::pending_info{};
		auto error = result.value ().deserialize (stream);
		release_assert (!error);
	}
	return result;
}

bool pending_view::exists (lumex::store::transaction const & txn, lumex::pending_key const & key) const
{
	return backend.exists (txn, lumex::store::table::pending, key);
}

bool pending_view::any (lumex::store::transaction const & txn, lumex::account const & account) const
{
	auto iterator = begin (txn, lumex::pending_key{ account, 0 });
	return iterator != end (txn) && lumex::pending_key (iterator->first).account == account;
}

auto pending_view::begin (lumex::store::transaction const & txn, lumex::pending_key const & key) const -> iterator
{
	return iterator{ backend.begin (txn, lumex::store::table::pending, key) };
}

auto pending_view::begin (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.begin (txn, lumex::store::table::pending) };
}

auto pending_view::end (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.end (txn, lumex::store::table::pending) };
}

void pending_view::for_each_par (std::function<void (lumex::store::read_transaction const &, iterator, iterator)> const & action) const
{
	parallel_traversal<lumex::uint512_t> (
	[&action, this] (lumex::uint512_t const & start, lumex::uint512_t const & end, bool const is_last) {
		lumex::uint512_union union_start{ start };
		lumex::uint512_union union_end{ end };
		lumex::pending_key key_start{ union_start.uint256s[0].number (), union_start.uint256s[1].number () };
		lumex::pending_key key_end{ union_end.uint256s[0].number (), union_end.uint256s[1].number () };
		auto txn = this->backend.tx_begin_read ();
		action (txn, this->begin (txn, key_start), !is_last ? this->begin (txn, key_end) : this->end (txn));
	});
}
}
