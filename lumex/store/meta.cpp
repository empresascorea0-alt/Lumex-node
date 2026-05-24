#include <lumex/store/backend.hpp>
#include <lumex/store/db_val.hpp>
#include <lumex/store/db_val_templ.hpp>
#include <lumex/store/meta.hpp>

#include <boost/multiprecision/cpp_int.hpp>

namespace lumex::store
{
meta_view::meta_view (lumex::store::backend & backend_a) :
	backend{ backend_a }
{
}

void meta_view::put_version (lumex::store::write_transaction const & txn, uint64_t version)
{
	put_value (txn, meta_key::version, lumex::uint256_union{ version });
}

uint64_t meta_view::get_version (lumex::store::transaction const & txn) const
{
	auto value = get_value (txn, meta_key::version);
	if (!value)
	{
		return 0;
	}
	debug_assert (value->qwords[2] == 0 && value->qwords[1] == 0 && value->qwords[0] == 0);
	return value->number ().convert_to<uint64_t> ();
}

bool meta_view::version_exists (lumex::store::transaction const & txn) const
{
	lumex::uint256_union db_key{ static_cast<uint64_t> (meta_key::version) };
	return backend.exists (txn, lumex::store::table::meta, db_key);
}

bool meta_view::get_flag (lumex::store::transaction const & txn, meta_key key) const
{
	auto value = get_value (txn, key);
	return value && value->number () != 0;
}

void meta_view::put_flag (lumex::store::write_transaction const & txn, meta_key key, bool value)
{
	put_value (txn, key, lumex::uint256_union{ value ? 1u : 0u });
}

std::optional<lumex::uint256_union> meta_view::get_value (lumex::store::transaction const & txn, meta_key key) const
{
	lumex::uint256_union db_key{ static_cast<uint64_t> (key) };
	lumex::store::db_val data;
	auto status = backend.get (txn, lumex::store::table::meta, db_key, data);
	if (!backend.success (status))
	{
		return std::nullopt;
	}
	return lumex::uint256_union{ data };
}

void meta_view::put_value (lumex::store::write_transaction const & txn, meta_key key, lumex::uint256_union const & value)
{
	lumex::uint256_union db_key{ static_cast<uint64_t> (key) };
	auto status = backend.put (txn, lumex::store::table::meta, db_key, value);
	backend.release_assert_success (status);
}
}
