#include <lumex/lib/assert.hpp>
#include <lumex/wallet/wallet_value.hpp>

lumex::wallet::wallet_value::wallet_value (lumex::store::db_val const & val_a)
{
	debug_assert (val_a.size () == sizeof (*this));
	std::copy (reinterpret_cast<uint8_t const *> (val_a.data ()), reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key), key.chars.begin ());
	std::copy (reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key), reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key) + sizeof (work), reinterpret_cast<char *> (&work));
}

lumex::wallet::wallet_value::wallet_value (lumex::raw_key const & key_a, uint64_t work_a) :
	key (key_a),
	work (work_a)
{
}

lumex::store::db_val lumex::wallet::wallet_value::val () const
{
	static_assert (sizeof (*this) == sizeof (key) + sizeof (work), "Class not packed");
	return lumex::store::db_val (sizeof (*this), this);
}

lumex::wallet::wallet_value::operator lumex::store::db_val () const
{
	return val ();
}
