#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/lib/timer.hpp>
#include <lumex/secure/fwd.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace lumex
{
class vote;

enum class vote_type
{
	normal,
	final
};

/**
 * Proof that a vote eligibility check was performed.
 * Can only be created by voting_policy — private constructor ensures callers cannot bypass the policy.
 */
class vote_permit
{
	friend class voting_policy;
	vote_permit (lumex::qualified_root qualified_root, lumex::block_hash hash, lumex::vote_type type);

	lumex::qualified_root qualified_root_m;
	lumex::block_hash hash_m;
	lumex::vote_type type_m;

public:
	lumex::qualified_root const & qualified_root () const;
	lumex::root root () const;
	lumex::block_hash const & hash () const;
	lumex::vote_type type () const;

	/// For tests
	static vote_permit dummy (lumex::qualified_root const & root, lumex::block_hash const & hash, lumex::vote_type type);
};

/**
 * Centralizes vote eligibility decisions and signing.
 * Single source of truth for whether a block should be voted on.
 * Vote construction must only be handled here.
 */
class voting_policy
{
public:
	explicit voting_policy (lumex::ledger &);

	/**
	 * Check vote eligibility for an already-fetched block.
	 * Returns a permit if the block's dependencies are cemented.
	 * If a final vote was previously recorded for this root, upgrades to a final vote permit for the already recorded hash.
	 */
	std::optional<vote_permit> vote (
	lumex::secure::transaction const &,
	lumex::block const & block) const;

	/**
	 * Check final vote eligibility for an already-fetched block.
	 * Returns a permit if dependencies are cemented and the final vote slot can be claimed (final_vote.put).
	 * Requires a write transaction because it persists the final vote decision.
	 */
	std::optional<vote_permit> vote_final (
	lumex::secure::write_transaction const &,
	lumex::block const & block) const;

	/**
	 * Read-only final vote eligibility for the reply path.
	 * If a final vote was previously recorded for this root, returns a permit for the *recorded* hash
	 * (which may differ from the queried block's hash in the case of forks).
	 * Falls back to allowing the block's own hash if the block is cemented.
	 * Does not persist any state.
	 */
	std::optional<vote_permit> reply_final (
	lumex::secure::transaction const &,
	lumex::block const & block) const;

	/**
	 * Signer iterates representative key pairs and calls the provided callback for each.
	 */
	using vote_signer_t = std::function<void (
	std::function<void (lumex::public_key const &, lumex::raw_key const &)> const &)>;

	/**
	 * Sign votes for the given permits.
	 * All permit hashes are batched into a single vote per representative.
	 * The signer provides representative key pairs via callback.
	 * Timestamp and duration are controlled based on vote type.
	 */
	std::vector<std::shared_ptr<lumex::vote>> sign (
	lumex::vote_type type,
	std::vector<vote_permit> const & permits,
	vote_signer_t const & signer,
	lumex::millis_t timestamp = lumex::milliseconds_since_epoch ()) const;

private:
	lumex::ledger & ledger;
};
}
