#include <lumex/lib/assert.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/config.hpp>
#include <lumex/lib/vote.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_set_any.hpp>
#include <lumex/secure/ledger_set_cemented.hpp>
#include <lumex/secure/voting_policy.hpp>
#include <lumex/store/ledger/final_vote.hpp>

/*
 * vote_permit
 */

lumex::vote_permit::vote_permit (lumex::qualified_root qualified_root, lumex::block_hash hash, lumex::vote_type type) :
	qualified_root_m{ qualified_root },
	hash_m{ hash },
	type_m{ type }
{
}

lumex::qualified_root const & lumex::vote_permit::qualified_root () const
{
	return qualified_root_m;
}

lumex::root lumex::vote_permit::root () const
{
	return qualified_root_m.root ();
}

lumex::block_hash const & lumex::vote_permit::hash () const
{
	return hash_m;
}

lumex::vote_type lumex::vote_permit::type () const
{
	return type_m;
}

lumex::vote_permit lumex::vote_permit::dummy (lumex::qualified_root const & root, lumex::block_hash const & hash, lumex::vote_type type)
{
	release_assert (lumex::is_dev_run ());
	return lumex::vote_permit{ root, hash, type };
}

/*
 * voting_policy
 */

lumex::voting_policy::voting_policy (lumex::ledger & ledger) :
	ledger{ ledger }
{
}

std::optional<lumex::vote_permit> lumex::voting_policy::vote (lumex::secure::transaction const & txn, lumex::block const & block) const
{
	if (ledger.dependencies_cemented (txn, block))
	{
		// If a final vote was already recorded for this root, upgrade to a final vote for the already recorded hash
		if (auto final_hash = ledger.store.final_vote.get (txn, block.qualified_root ()))
		{
			return lumex::vote_permit{ block.qualified_root (), *final_hash, vote_type::final };
		}
		else // No final vote recorded, proceed with a normal vote
		{
			return lumex::vote_permit{ block.qualified_root (), block.hash (), vote_type::normal };
		}
	}
	return std::nullopt;
}

std::optional<lumex::vote_permit> lumex::voting_policy::vote_final (lumex::secure::write_transaction const & txn, lumex::block const & block) const
{
	if (ledger.dependencies_cemented (txn, block))
	{
		// This checks for existing final vote and only inserts if no record exists, or if the same hash is already recorded for this root
		if (ledger.store.final_vote.put (txn, block.qualified_root (), block.hash ()))
		{
			return lumex::vote_permit{ block.qualified_root (), block.hash (), vote_type::final };
		}
	}
	return std::nullopt;
}

std::optional<lumex::vote_permit> lumex::voting_policy::reply_final (lumex::secure::transaction const & txn, lumex::block const & block) const
{
	// If a final vote was already committed for this root, allow voting for the recorded hash
	if (auto final_hash = ledger.store.final_vote.get (txn, block.qualified_root ()))
	{
		return lumex::vote_permit{ block.qualified_root (), *final_hash, vote_type::final };
	}
	// If no final vote recorded but block is already cemented, allow voting for this hash
	if (ledger.cemented.block_exists (txn, block.hash ()))
	{
		return lumex::vote_permit{ block.qualified_root (), block.hash (), vote_type::final };
	}
	return std::nullopt;
}

std::vector<std::shared_ptr<lumex::vote>> lumex::voting_policy::sign (lumex::vote_type type, std::vector<lumex::vote_permit> const & permits, vote_signer_t const & signer, lumex::millis_t timestamp) const
{
	if (permits.empty ())
	{
		return {};
	}

	release_assert (std::all_of (permits.begin (), permits.end (), [type] (auto const & p) { return p.type () == type; }), "vote permit type mismatch");

	std::vector<lumex::block_hash> hashes;
	hashes.reserve (permits.size ());
	for (auto const & permit : permits)
	{
		hashes.push_back (permit.hash ());
	}

	lumex::millis_t const effective_timestamp = (type == vote_type::final) ? lumex::vote::timestamp_max : timestamp;
	uint8_t const effective_duration = (type == vote_type::final) ? lumex::vote::duration_max : /*8192ms*/ 0x9;

	std::vector<std::shared_ptr<lumex::vote>> votes;
	signer ([&] (lumex::public_key const & pub, lumex::raw_key const & prv) {
		votes.emplace_back (std::make_shared<lumex::vote> (pub, prv, effective_timestamp, effective_duration, hashes));
	});
	return votes;
}
