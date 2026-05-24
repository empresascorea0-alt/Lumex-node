#include <lumex/lib/numbers.hpp>
#include <lumex/secure/rep_weights.hpp>
#include <lumex/store/ledger/rep_weight.hpp>

lumex::rep_weights::rep_weights (lumex::store::ledger::rep_weight_view & rep_weight_store_a, lumex::uint128_t min_weight_a) :
	rep_weight_store{ rep_weight_store_a },
	min_weight{ min_weight_a }
{
}

void lumex::rep_weights::add (store::write_transaction const & txn, lumex::account const & rep, lumex::uint128_t const & amount_add)
{
	auto const previous_weight = rep_weight_store.get (txn, rep);
	auto const new_weight = previous_weight + amount_add;
	release_assert (new_weight >= previous_weight, "new weight must be greater than or equal to previous weight");

	put_store (txn, rep, previous_weight, new_weight);

	std::lock_guard guard{ mutex };
	put_cache (rep, new_weight);

	weight_committed += amount_add;
	weight_unused -= amount_add;
}

void lumex::rep_weights::sub (store::write_transaction const & txn, lumex::account const & rep, lumex::uint128_t const & amount_sub)
{
	auto const previous_weight = rep_weight_store.get (txn, rep);
	auto const new_weight = previous_weight - amount_sub;
	release_assert (new_weight <= previous_weight, "new weight must be less than or equal to previous weight");

	put_store (txn, rep, previous_weight, new_weight);

	std::lock_guard guard{ mutex };
	put_cache (rep, new_weight);

	weight_committed -= amount_sub;
	weight_unused += amount_sub;
}

void lumex::rep_weights::move (store::write_transaction const & txn, lumex::account const & source_rep, lumex::account const & dest_rep, lumex::uint128_t const & amount)
{
	if (source_rep == dest_rep) // Nothing to move if reps are the same
	{
		return;
	}

	auto const previous_weight_source = rep_weight_store.get (txn, source_rep);
	auto const previous_weight_dest = rep_weight_store.get (txn, dest_rep);
	release_assert (previous_weight_source >= amount, "source representative must have enough weight to move");

	auto const new_weight_source = previous_weight_source - amount;
	auto const new_weight_dest = previous_weight_dest + amount;
	release_assert (new_weight_dest >= previous_weight_dest, "new weight for destination representative must be greater than or equal to previous weight");
	release_assert (new_weight_source <= previous_weight_source, "new weight for source representative must be less than or equal to previous weight");

	put_store (txn, source_rep, previous_weight_source, new_weight_source);
	put_store (txn, dest_rep, previous_weight_dest, new_weight_dest);

	std::lock_guard guard{ mutex };
	put_cache (source_rep, new_weight_source);
	put_cache (dest_rep, new_weight_dest);
}

void lumex::rep_weights::move_add_sub (store::write_transaction const & txn, lumex::account const & source_rep, lumex::uint128_t const & amount_source, lumex::account const & dest_rep, lumex::uint128_t const & amount_dest)
{
	if (amount_source == amount_dest)
	{
		move (txn, source_rep, dest_rep, amount_source);
	}
	else if (amount_dest > amount_source)
	{
		move (txn, source_rep, dest_rep, amount_source);
		add (txn, dest_rep, amount_dest - amount_source);
	}
	else if (amount_source > amount_dest)
	{
		move (txn, source_rep, dest_rep, amount_dest);
		sub (txn, source_rep, amount_source - amount_dest);
	}
	else
	{
		release_assert (false);
	}
}

void lumex::rep_weights::put (lumex::account const & rep, lumex::uint128_t const & weight)
{
	std::lock_guard guard{ mutex };
	put_cache (rep, weight);
	weight_committed += weight;
}

void lumex::rep_weights::put_unused (lumex::uint128_t const & weight)
{
	std::lock_guard guard{ mutex };
	weight_unused += weight;
}

lumex::uint128_t lumex::rep_weights::get (lumex::account const & rep) const
{
	std::shared_lock guard{ mutex };
	return get_impl (rep);
}

std::unordered_map<lumex::account, lumex::uint128_t> lumex::rep_weights::get_rep_amounts () const
{
	std::shared_lock guard{ mutex };
	return rep_amounts;
}

void lumex::rep_weights::append_from (lumex::rep_weights const & other)
{
	std::lock_guard guard_this{ mutex };
	std::shared_lock guard_other{ other.mutex };
	for (auto const & entry : other.rep_amounts)
	{
		auto prev_amount = get_impl (entry.first);
		put_cache (entry.first, prev_amount + entry.second);
	}
	weight_committed += other.weight_committed;
	weight_unused += other.weight_unused;
}

void lumex::rep_weights::verify_consistency (lumex::uint128_t const burn_balance) const
{
	std::shared_lock guard{ mutex };

	auto const total_weight = weight_committed + weight_unused;
	release_assert (total_weight == std::numeric_limits<lumex::uint128_t>::max (), "total weight exceeds maximum value", to_string (weight_committed) + " + " + to_string (weight_unused));

	auto const expected_total = std::numeric_limits<lumex::uint128_t>::max () - burn_balance;
	release_assert (weight_committed <= expected_total, "total weight does not match expected value accounting for burn", to_string (weight_committed) + " + " + to_string (weight_unused) + " != " + to_string (expected_total) + " (burn: " + to_string (burn_balance) + ")");

	auto const cached_weight = std::accumulate (rep_amounts.begin (), rep_amounts.end (), lumex::uint256_t{ 0 }, [] (lumex::uint256_t sum, const auto & entry) {
		return sum + entry.second;
	});
	release_assert (cached_weight <= weight_committed, "total cached weight must match the sum of all committed weights", to_string (cached_weight) + " <= " + to_string (weight_committed));
}

void lumex::rep_weights::put_cache (lumex::account const & rep, lumex::uint128_union const & weight)
{
	debug_assert (!mutex.try_lock ());

	auto it = rep_amounts.find (rep);
	if (weight < min_weight || weight.is_zero ())
	{
		if (it != rep_amounts.end ())
		{
			rep_amounts.erase (it);
		}
	}
	else
	{
		auto amount = weight.number ();
		if (it != rep_amounts.end ())
		{
			it->second = amount;
		}
		else
		{
			rep_amounts.emplace (rep, amount);
		}
	}
}

void lumex::rep_weights::put_store (store::write_transaction const & txn, lumex::account const & rep, lumex::uint128_t const & previous_weight, lumex::uint128_t const & new_weight)
{
	debug_assert (rep_weight_store.get (txn, rep) == previous_weight);
	if (new_weight.is_zero ())
	{
		if (!previous_weight.is_zero ())
		{
			rep_weight_store.del (txn, rep);
		}
	}
	else
	{
		rep_weight_store.put (txn, rep, new_weight);
	}
}

lumex::uint128_t lumex::rep_weights::get_impl (lumex::account const & rep) const
{
	if (rep.is_zero ())
	{
		return 0; // Zero account always has zero weight
	}

	auto it = rep_amounts.find (rep);
	if (it != rep_amounts.end ())
	{
		return it->second;
	}
	else
	{
		return lumex::uint128_t{ 0 };
	}
}

std::size_t lumex::rep_weights::size () const
{
	std::shared_lock guard{ mutex };
	return rep_amounts.size ();
}

bool lumex::rep_weights::empty () const
{
	std::shared_lock guard{ mutex };
	return rep_amounts.empty () && weight_committed.is_zero () && weight_unused.is_zero ();
}

lumex::uint128_t lumex::rep_weights::get_weight_committed () const
{
	std::shared_lock guard{ mutex };
	release_assert (weight_committed <= std::numeric_limits<lumex::uint128_t>::max (), "weight committed exceeds maximum uint128_t value");
	return static_cast<lumex::uint128_t> (weight_committed);
}

lumex::uint128_t lumex::rep_weights::get_weight_unused () const
{
	std::shared_lock guard{ mutex };
	release_assert (weight_unused <= std::numeric_limits<lumex::uint128_t>::max (), "weight unused exceeds maximum uint128_t value");
	return static_cast<lumex::uint128_t> (weight_unused);
}

lumex::container_info lumex::rep_weights::container_info () const
{
	std::shared_lock guard{ mutex };

	lumex::container_info info;
	info.put ("rep_amounts", rep_amounts);
	// TODO: Info about weight_committed and weight_unused
	return info;
}
