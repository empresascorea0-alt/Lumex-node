#pragma once

#include <lumex/lib/epoch.hpp>
#include <lumex/lib/fwd.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/secure/fwd.hpp>

#include <boost/multiprecision/cpp_int.hpp>

namespace lumex
{
/**
 * Information on an uncollected send
 * This class captures the data stored in a pending table entry
 */
class pending_info final
{
public:
	pending_info () = default;
	pending_info (lumex::account const &, lumex::amount const &, lumex::epoch);
	size_t db_size () const;
	bool deserialize (lumex::stream &);
	bool operator== (lumex::pending_info const &) const;
	lumex::account source{}; // the account sending the funds
	lumex::amount amount{ 0 }; // amount receivable in this transaction
	lumex::epoch epoch{ lumex::epoch::epoch_0 }; // epoch of sending block, this info is stored here to make it possible to prune the send block

	friend std::ostream & operator<< (std::ostream & os, const lumex::pending_info & info)
	{
		const int epoch = lumex::normalized_epoch (info.epoch);
		os << "Source: " << info.source << ", Amount: " << info.amount.to_string_dec () << " Epoch: " << epoch;
		return os;
	}
};

// This class represents the data written into the pending (receivable) database table key
// the receiving account and hash of the send block identify a pending db table entry
class pending_key final
{
public:
	pending_key () = default;
	pending_key (lumex::account const &, lumex::block_hash const &);
	bool deserialize (lumex::stream &);
	bool operator== (lumex::pending_key const &) const;
	bool operator< (lumex::pending_key const &) const;
	lumex::account const & key () const;
	lumex::account account{}; // receiving account
	lumex::block_hash hash{ 0 }; // hash of the send block

	friend std::ostream & operator<< (std::ostream & os, const lumex::pending_key & key)
	{
		os << "Account: " << key.account << ", Hash: " << key.hash;
		return os;
	}
};
}

namespace std
{
template <>
struct hash<::lumex::pending_key>
{
	size_t operator() (::lumex::pending_key const & value) const
	{
		return hash<::lumex::uint512_union>{}({ ::lumex::uint256_union{ value.account.number () }, value.hash });
	}
};
}
