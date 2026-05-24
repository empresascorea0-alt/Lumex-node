#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/secure/fwd.hpp>

#include <boost/multiprecision/cpp_int.hpp>

#include <memory>
#include <shared_mutex>
#include <unordered_map>

namespace lumex
{
class rep_weights
{
public:
	explicit rep_weights (lumex::store::ledger::rep_weight_view &, lumex::uint128_t min_weight = 0);

	/* Adds or subtracts weight to the representative */
	void add (store::write_transaction const &, lumex::account const & rep, lumex::uint128_t const & amount_add);
	void sub (store::write_transaction const &, lumex::account const & rep, lumex::uint128_t const & amount_sub);

	/* Move weight from one representative to another */
	void move (store::write_transaction const &, lumex::account const & source_rep, lumex::account const & dest_rep, lumex::uint128_t const & amount);

	/* Move weight from one representative to another while adding or subtracting the weight */
	void move_add_sub (store::write_transaction const &, lumex::account const & source_rep, lumex::uint128_t const & amount_source, lumex::account const & dest_rep, lumex::uint128_t const & amount_dest);

	/* Only use this method when loading rep weights from the database table */
	void put (lumex::account const & rep, lumex::uint128_t const & weight);
	void put_unused (lumex::uint128_t const & weight);
	void append_from (rep_weights const & other);

	lumex::uint128_t get (lumex::account const & rep) const;
	std::unordered_map<lumex::account, lumex::uint128_t> get_rep_amounts () const;

	size_t size () const;
	lumex::container_info container_info () const;
	bool empty () const;

	lumex::uint128_t get_weight_committed () const;
	lumex::uint128_t get_weight_unused () const;

	void verify_consistency (lumex::uint128_t burn_balance) const;

private:
	lumex::store::ledger::rep_weight_view & rep_weight_store;
	lumex::uint128_t const min_weight;

	mutable std::shared_mutex mutex;
	std::unordered_map<lumex::account, lumex::uint128_t> rep_amounts;

	// Used for consistency checking, use higher precision types to detect overflows
	lumex::uint256_t weight_committed{ 0 };
	lumex::uint256_t weight_unused{ 0 };

private:
	void put_cache (lumex::account const & rep, lumex::uint128_union const & weight);
	void put_store (store::write_transaction const &, lumex::account const & rep, lumex::uint128_t const & previous_weight, lumex::uint128_t const & new_weight);
	lumex::uint128_t get_impl (lumex::account const & rep) const;
};
}
