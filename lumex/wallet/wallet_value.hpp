#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/store/db_val.hpp>

namespace lumex::wallet
{
class wallet_value
{
public:
	wallet_value () = default;
	wallet_value (store::db_val const &);
	wallet_value (lumex::raw_key const &, uint64_t);

	store::db_val val () const;
	operator store::db_val () const;

public:
	lumex::raw_key key;
	uint64_t work;
};
}
