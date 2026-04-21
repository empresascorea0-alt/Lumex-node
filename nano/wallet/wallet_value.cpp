#include <nano/lib/assert.hpp>
#include <nano/wallet/wallet_value.hpp>

nano::wallet::wallet_value::wallet_value (nano::store::db_val const & val_a)
{
	debug_assert (val_a.size () == sizeof (*this));
	std::copy (reinterpret_cast<uint8_t const *> (val_a.data ()), reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key), key.chars.begin ());
	std::copy (reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key), reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key) + sizeof (work), reinterpret_cast<char *> (&work));
}

nano::wallet::wallet_value::wallet_value (nano::raw_key const & key_a, uint64_t work_a) :
	key (key_a),
	work (work_a)
{
}

nano::store::db_val nano::wallet::wallet_value::val () const
{
	static_assert (sizeof (*this) == sizeof (key) + sizeof (work), "Class not packed");
	return nano::store::db_val (sizeof (*this), this);
}

nano::wallet::wallet_value::operator nano::store::db_val () const
{
	return val ();
}
