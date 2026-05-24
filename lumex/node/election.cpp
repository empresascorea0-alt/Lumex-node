#include <lumex/lib/blocks.hpp>
#include <lumex/lib/enum_util.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/vote.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/block_processor.hpp>
#include <lumex/node/block_rebroadcaster.hpp>
#include <lumex/node/cementing_set.hpp>
#include <lumex/node/confirmation_solicitor.hpp>
#include <lumex/node/election.hpp>
#include <lumex/node/local_vote_history.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/online_reps.hpp>
#include <lumex/node/vote_cache.hpp>
#include <lumex/node/vote_generator.hpp>
#include <lumex/node/vote_router.hpp>
#include <lumex/node/wallet.hpp>
#include <lumex/secure/ledger.hpp>

using namespace std::chrono;

std::chrono::milliseconds lumex::election::base_latency () const
{
	return node.network_params.network.is_dev_network () ? 25ms : 1000ms;
}

/*
 * election
 */

lumex::election::election (lumex::node & node_a, std::shared_ptr<lumex::block> const & block_a, lumex::election_behavior election_behavior_a, lumex::bucket_index bucket_a, std::function<void (std::shared_ptr<lumex::block> const &)> confirmation_action_a, std::function<void (lumex::account const &)> vote_action_a, std::function<void (lumex::qualified_root const &)> update_action_a) :
	confirmation_action (std::move (confirmation_action_a)),
	vote_action (std::move (vote_action_a)),
	update_action (std::move (update_action_a)),
	node (node_a),
	behavior_m (election_behavior_a),
	status (block_a),
	height (block_a->sideband ().height),
	root (block_a->root ()),
	qualified_root (block_a->qualified_root ()),
	account (block_a->account ()),
	bucket (bucket_a)
{
	last_votes.emplace (lumex::account::null (), lumex::vote_info{ std::chrono::steady_clock::now (), 0, block_a->hash () });
	last_blocks.emplace (block_a->hash (), block_a);
}

void lumex::election::confirm_once (lumex::unique_lock<lumex::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());

	bool just_confirmed = state_m != lumex::election_state::confirmed;
	state_m = lumex::election_state::confirmed;
	state_start = std::chrono::steady_clock::now ();

	if (just_confirmed)
	{
		status.election_end = std::chrono::system_clock::now (); // Timestamp as system time
		status.election_duration = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - election_start);
		status.confirmation_request_count = confirmation_request_count;
		status.block_count = lumex::narrow_cast<decltype (status.block_count)> (last_blocks.size ());
		status.voter_count = lumex::narrow_cast<decltype (status.voter_count)> (last_votes.size ());
		auto const status_l = status;

		node.active.recently_confirmed.put (qualified_root, status_l.winner->hash (), status_l);

		auto const extended_status = current_status_locked ();

		node.stats.inc (lumex::stat::type::election, lumex::stat::detail::confirm_once);
		node.logger.trace (lumex::log::type::election, lumex::log::detail::election_confirmed,
		lumex::log::arg{ "id", id },
		lumex::log::arg{ "qualified_root", qualified_root },
		lumex::log::arg{ "status", extended_status });

		node.logger.debug (lumex::log::type::election, "Election confirmed with winner: {} (behavior: {}, state: {}, voters: {}, blocks: {}, duration: {}ms, confirmation requests: {})",
		status_l.winner->hash (),
		to_string (behavior_m),
		to_string (state_m),
		extended_status.status.voter_count,
		extended_status.status.block_count,
		extended_status.status.election_duration.count (),
		extended_status.status.confirmation_request_count);

		node.cementing_set.add (status_l.winner->hash (), shared_from_this ());

		lock.unlock ();

		if (update_action)
		{
			node.election_workers.post ([qualified_root_l = qualified_root, update_action_l = update_action] () {
				update_action_l (qualified_root_l);
			});
		}

		if (confirmation_action)
		{
			node.election_workers.post ([status_l, confirmation_action_l = confirmation_action] () {
				confirmation_action_l (status_l.winner);
			});
		}
	}
	else
	{
		node.stats.inc (lumex::stat::type::election, lumex::stat::detail::confirm_once_failed);
		lock.unlock ();
	}
}

bool lumex::election::valid_change (lumex::election_state expected_a, lumex::election_state desired_a) const
{
	switch (expected_a)
	{
		case lumex::election_state::passive:
			switch (desired_a)
			{
				case lumex::election_state::active:
				case lumex::election_state::confirmed:
				case lumex::election_state::expired_unconfirmed:
				case lumex::election_state::cancelled:
					return true; // Valid
				default:
					break;
			}
			break;
		case lumex::election_state::active:
			switch (desired_a)
			{
				case lumex::election_state::confirmed:
				case lumex::election_state::expired_unconfirmed:
				case lumex::election_state::cancelled:
					return true; // Valid
				default:
					break;
			}
			break;
		case lumex::election_state::confirmed:
			switch (desired_a)
			{
				case lumex::election_state::expired_confirmed:
					return true; // Valid
				default:
					break;
			}
			break;
		case lumex::election_state::expired_unconfirmed:
		case lumex::election_state::expired_confirmed:
		case lumex::election_state::cancelled:
			// No transitions are valid from these states
			break;
	}
	return false;
}

bool lumex::election::state_change (lumex::election_state expected_a, lumex::election_state desired_a)
{
	bool result = true;
	if (valid_change (expected_a, desired_a))
	{
		if (state_m == expected_a)
		{
			state_m = desired_a;
			state_start = std::chrono::steady_clock::now ();
			result = false;

			if (update_action)
			{
				node.election_workers.post ([qualified_root_l = qualified_root, update_action_l = update_action] () {
					update_action_l (qualified_root_l);
				});
			}
		}
	}
	return result;
}

std::chrono::milliseconds lumex::election::confirm_req_time () const
{
	switch (behavior_m)
	{
		case election_behavior::manual:
		case election_behavior::priority:
		case election_behavior::hinted:
			return base_latency () * 5;
		case election_behavior::optimistic:
			return base_latency () * 2;
	}
	debug_assert (false);
	return {};
}

void lumex::election::send_confirm_req (lumex::confirmation_solicitor & solicitor_a)
{
	debug_assert (!mutex.try_lock ());

	if (confirm_req_time () < (std::chrono::steady_clock::now () - last_req))
	{
		if (!solicitor_a.add (*this))
		{
			last_req = std::chrono::steady_clock::now ();
			++confirmation_request_count;

			node.stats.inc (lumex::stat::type::election, lumex::stat::detail::confirmation_request);
			node.logger.debug (lumex::log::type::election, "Sent confirmation request for root: {} (behavior: {}, state: {}, voters: {}, blocks: {}, duration: {}ms, confirmation requests: {})",
			qualified_root,
			to_string (behavior_m),
			to_string (state_m),
			status.voter_count,
			status.block_count,
			duration ().count (),
			confirmation_request_count.load ());
		}
	}
}

bool lumex::election::transition_priority ()
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	if (behavior_m == lumex::election_behavior::priority || behavior_m == lumex::election_behavior::manual)
	{
		return false;
	}

	behavior_m = lumex::election_behavior::priority;
	last_vote = std::chrono::steady_clock::time_point{}; // allow new outgoing votes immediately

	node.logger.debug (lumex::log::type::election, "Transitioned election behavior to priority from {} for root: {} (duration: {}ms)",
	to_string (behavior_m),
	qualified_root,
	duration ().count ());

	if (update_action)
	{
		node.election_workers.post ([qualified_root_l = qualified_root, update_action_l = update_action] () {
			update_action_l (qualified_root_l);
		});
	}

	return true;
}

bool lumex::election::transition_active ()
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return !state_change (lumex::election_state::passive, lumex::election_state::active); // Invert since false => success
}

bool lumex::election::cancel ()
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return !state_change (state_m, lumex::election_state::cancelled); // Invert since false => success
}

bool lumex::election::confirmed_locked () const
{
	debug_assert (!mutex.try_lock ());
	return state_m == lumex::election_state::confirmed || state_m == lumex::election_state::expired_confirmed;
}

bool lumex::election::confirmed () const
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	return confirmed_locked ();
}

bool lumex::election::failed () const
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	return state_m == lumex::election_state::expired_unconfirmed;
}

bool lumex::election::broadcast_block_predicate () const
{
	debug_assert (!mutex.try_lock ());

	// Broadcast the block if enough time has passed since the last broadcast (or it's the first broadcast)
	if (last_block + node.config.network_params.network.block_broadcast_interval < std::chrono::steady_clock::now ())
	{
		return true;
	}
	// Or the current election winner has changed
	if (status.winner->hash () != last_block_hash)
	{
		return true;
	}
	return false;
}

void lumex::election::broadcast_block (lumex::confirmation_solicitor & solicitor_a)
{
	debug_assert (!mutex.try_lock ());

	if (broadcast_block_predicate ())
	{
		if (!solicitor_a.broadcast (*this))
		{
			last_block = std::chrono::steady_clock::now ();
			last_block_hash = status.winner->hash ();

			node.stats.inc (lumex::stat::type::election, last_block_hash.is_zero () ? lumex::stat::detail::broadcast_block_initial : lumex::stat::detail::broadcast_block_repeat);

			node.logger.debug (lumex::log::type::election, "Broadcasted current winner: {} for root: {} (behavior: {}, state: {}, voters: {}, blocks: {}, duration: {}ms)",
			status.winner->hash (),
			qualified_root,
			to_string (behavior_m),
			to_string (state_m),
			status.voter_count,
			status.block_count,
			duration ().count ());

			// Random flood for block propagation
			node.block_rebroadcaster.push (status.winner);
		}
	}
}

void lumex::election::broadcast_vote ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	broadcast_vote_locked (lock);
}

lumex::vote_info lumex::election::get_last_vote (lumex::account const & account)
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return last_votes[account];
}

void lumex::election::set_last_vote (lumex::account const & account, lumex::vote_info vote_info)
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	last_votes[account] = vote_info;
}

lumex::election_status lumex::election::get_status () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return status;
}

bool lumex::election::tick (lumex::confirmation_solicitor & solicitor)
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	bool result = false;
	switch (state_m)
	{
		case lumex::election_state::passive:
			if (base_latency () * passive_duration_factor < std::chrono::steady_clock::now () - state_start)
			{
				state_change (lumex::election_state::passive, lumex::election_state::active);
			}
			break;
		case lumex::election_state::active:
			broadcast_vote_locked (lock);
			broadcast_block (solicitor);
			send_confirm_req (solicitor);
			break;
		case lumex::election_state::confirmed:
			result = true; // Return true to indicate this election should be cleaned up
			broadcast_block (solicitor); // Ensure election winner is broadcasted
			state_change (lumex::election_state::confirmed, lumex::election_state::expired_confirmed);
			break;
		case lumex::election_state::expired_unconfirmed:
		case lumex::election_state::expired_confirmed:
			debug_assert (false);
			break;
		case lumex::election_state::cancelled:
			return true; // Clean up cancelled elections immediately
	}

	if (!confirmed_locked () && time_to_live () < std::chrono::steady_clock::now () - election_start)
	{
		// It is possible the election confirmed while acquiring the mutex
		// state_change returning true would indicate it
		if (!state_change (state_m, lumex::election_state::expired_unconfirmed))
		{
			node.logger.trace (lumex::log::type::election, lumex::log::detail::election_expired,
			lumex::log::arg{ "id", id },
			lumex::log::arg{ "qualified_root", qualified_root },
			lumex::log::arg{ "status", current_status_locked () });

			result = true; // Return true to indicate this election should be cleaned up
			status.type = lumex::election_status_type::stopped;
		}
	}

	return result;
}

std::chrono::milliseconds lumex::election::time_to_live () const
{
	switch (behavior_m)
	{
		case election_behavior::manual:
		case election_behavior::priority:
			return std::chrono::milliseconds (5 * 60 * 1000);
		case election_behavior::hinted:
		case election_behavior::optimistic:
			return std::chrono::milliseconds (30 * 1000);
	}
	debug_assert (false);
	return {};
}

std::chrono::seconds lumex::election::cooldown_time (lumex::uint128_t weight) const
{
	auto online_stake = node.online_reps.trended ();
	if (weight > online_stake / 20) // Reps with more than 5% weight
	{
		return std::chrono::seconds{ 1 };
	}
	if (weight > online_stake / 100) // Reps with more than 1% weight
	{
		return std::chrono::seconds{ 5 };
	}
	// The rest of smaller reps
	return std::chrono::seconds{ 15 };
}

bool lumex::election::have_quorum (lumex::tally_t const & tally_a) const
{
	auto i (tally_a.begin ());
	++i;
	auto second (i != tally_a.end () ? i->first : 0);
	auto delta_l (node.online_reps.delta ());
	release_assert (tally_a.begin ()->first >= second);
	bool result{ (tally_a.begin ()->first - second) >= delta_l };
	return result;
}

lumex::tally_t lumex::election::tally () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return tally_impl ();
}

lumex::tally_t lumex::election::tally_impl () const
{
	std::unordered_map<lumex::block_hash, lumex::uint128_t> block_weights;
	std::unordered_map<lumex::block_hash, lumex::uint128_t> final_weights_l;
	for (auto const & [account, info] : last_votes)
	{
		auto rep_weight (node.ledger.weight (account));
		block_weights[info.hash] += rep_weight;
		if (info.timestamp == std::numeric_limits<uint64_t>::max ())
		{
			final_weights_l[info.hash] += rep_weight;
		}
	}
	last_tally = block_weights;
	lumex::tally_t result;
	for (auto const & [hash, amount] : block_weights)
	{
		auto block (last_blocks.find (hash));
		if (block != last_blocks.end ())
		{
			result.emplace (amount, block->second);
		}
	}
	// Calculate final votes sum for winner
	if (!final_weights_l.empty () && !result.empty ())
	{
		auto winner_hash (result.begin ()->second->hash ());
		auto find_final (final_weights_l.find (winner_hash));
		if (find_final != final_weights_l.end ())
		{
			final_weight = find_final->second;
		}
	}
	return result;
}

void lumex::election::confirm_if_quorum (lumex::unique_lock<lumex::mutex> & lock_a)
{
	debug_assert (lock_a.owns_lock ());
	auto tally_l (tally_impl ());
	release_assert (!tally_l.empty ());
	auto winner (tally_l.begin ());
	auto block_l (winner->second);
	auto const & winner_hash_l (block_l->hash ());
	status.tally = winner->first;
	status.final_tally = final_weight;
	auto const & status_winner_hash_l (status.winner->hash ());
	lumex::uint128_t sum (0);
	for (auto & i : tally_l)
	{
		sum += i.first;
	}
	if (sum >= node.online_reps.delta () && winner_hash_l != status_winner_hash_l)
	{
		status.winner = block_l;
		remove_votes (status_winner_hash_l);

		node.logger.debug (lumex::log::type::election, "Winning fork changed from {} to {} for root: {} (behavior: {}, state: {}, voters: {}, blocks: {}, duration: {}ms)",
		status_winner_hash_l,
		winner_hash_l,
		qualified_root,
		to_string (behavior_m),
		to_string (state_m),
		status.voter_count,
		status.block_count,
		duration ().count ());

		node.block_processor.force (block_l);
	}
	if (have_quorum (tally_l))
	{
		if (!is_quorum.exchange (true) && node.config.enable_voting && node.wallets.reps ().voting > 0)
		{
			++vote_broadcast_count;
			node.vote_generator.vote_final (qualified_root, status.winner->hash (), bucket);
		}
		if (final_weight >= node.online_reps.delta ())
		{
			// In some edge cases block might get rolled back while the election is confirming, reprocess it to ensure it's present in the ledger
			node.block_processor.add (block_l, lumex::block_source::election);
			confirm_once (lock_a);
			debug_assert (!lock_a.owns_lock ());
		}
	}
}

void lumex::election::try_confirm (lumex::block_hash const & hash)
{
	lumex::unique_lock<lumex::mutex> election_lock{ mutex };
	auto winner = status.winner;
	if (winner && winner->hash () == hash)
	{
		if (!confirmed_locked ())
		{
			confirm_once (election_lock);
			debug_assert (!election_lock.owns_lock ());
		}
	}
}

std::shared_ptr<lumex::block> lumex::election::find (lumex::block_hash const & hash_a) const
{
	std::shared_ptr<lumex::block> result;
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	if (auto existing = last_blocks.find (hash_a); existing != last_blocks.end ())
	{
		result = existing->second;
	}
	return result;
}

lumex::vote_code lumex::election::vote (lumex::account const & rep, uint64_t timestamp_a, lumex::block_hash const & block_hash_a, lumex::vote_source vote_source_a)
{
	auto const weight = node.ledger.weight (rep);

	if (!node.network_params.network.is_dev_network () && weight <= node.minimum_principal_weight ())
	{
		return vote_code::indeterminate;
	}

	lumex::unique_lock<lumex::mutex> lock{ mutex };

	auto last_vote_it (last_votes.find (rep));
	if (last_vote_it != last_votes.end ())
	{
		auto last_vote_l (last_vote_it->second);
		if (last_vote_l.timestamp > timestamp_a)
		{
			return vote_code::replay;
		}
		if (last_vote_l.timestamp == timestamp_a && !(last_vote_l.hash < block_hash_a))
		{
			return vote_code::replay;
		}

		auto max_vote = timestamp_a == std::numeric_limits<uint64_t>::max () && last_vote_l.timestamp < timestamp_a;

		bool past_cooldown = true;
		if (vote_source_a != vote_source::cache) // Only cooldown live votes
		{
			const auto cooldown = cooldown_time (weight);
			past_cooldown = last_vote_l.time <= std::chrono::steady_clock::now () - cooldown;
		}

		if (!max_vote && !past_cooldown)
		{
			return vote_code::ignored;
		}
	}

	// Update voter list entry
	last_votes[rep] = { std::chrono::steady_clock::now (), timestamp_a, block_hash_a };

	node.stats.inc (lumex::stat::type::election, lumex::stat::detail::vote);
	node.stats.inc (lumex::stat::type::election_vote, to_stat_detail (vote_source_a));

	node.logger.trace (lumex::log::type::election, lumex::log::detail::vote_processed,
	lumex::log::arg{ "id", id },
	lumex::log::arg{ "qualified_root", qualified_root },
	lumex::log::arg{ "account", rep },
	lumex::log::arg{ "hash", block_hash_a },
	lumex::log::arg{ "final", lumex::vote::is_final_timestamp (timestamp_a) },
	lumex::log::arg{ "timestamp", timestamp_a },
	lumex::log::arg{ "vote_source", vote_source_a },
	lumex::log::arg{ "weight", weight });

	node.logger.debug (lumex::log::type::election, "Vote received for hash: {} from: {} for root: {} (final: {}, weight: {}, source: {})",
	block_hash_a,
	rep,
	qualified_root,
	lumex::vote::is_final_timestamp (timestamp_a),
	weight,
	to_string (vote_source_a));

	// This must execute before calculating the vote tally to ensure accurate online weight and quorum numbers are used
	if (vote_action)
	{
		vote_action (rep);
	}

	if (!confirmed_locked ())
	{
		confirm_if_quorum (lock);
	}

	return vote_code::vote;
}

bool lumex::election::publish (std::shared_ptr<lumex::block> const & block_a)
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };

	// Do not insert new blocks if already confirmed
	auto result (confirmed_locked ());
	if (!result && last_blocks.size () >= max_blocks && last_blocks.find (block_a->hash ()) == last_blocks.end ())
	{
		if (!replace_by_weight (lock, block_a->hash ()))
		{
			result = true;
			node.network.filter.clear (block_a);
		}
		debug_assert (lock.owns_lock ());
	}
	if (!result)
	{
		auto existing = last_blocks.find (block_a->hash ());
		if (existing == last_blocks.end ())
		{
			last_blocks.emplace (std::make_pair (block_a->hash (), block_a));
		}
		else
		{
			result = true;
			existing->second = block_a;
			if (status.winner->hash () == block_a->hash ())
			{
				status.winner = block_a;
			}
		}
	}
	/*
	Result is true if:
	1) election is confirmed or expired
	2) given election contains 10 blocks & new block didn't receive enough votes to replace existing blocks
	3) given block in already in election & election contains less than 10 blocks (replacing block content with new)
	*/
	return result;
}

lumex::election_extended_status lumex::election::current_status () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return current_status_locked ();
}

lumex::election_extended_status lumex::election::current_status_locked () const
{
	debug_assert (!mutex.try_lock ());

	lumex::election_status status_l = status;
	status_l.confirmation_request_count = confirmation_request_count;
	status_l.vote_broadcast_count = vote_broadcast_count;
	status_l.block_count = lumex::narrow_cast<decltype (status_l.block_count)> (last_blocks.size ());
	status_l.voter_count = lumex::narrow_cast<decltype (status_l.voter_count)> (last_votes.size ());
	return lumex::election_extended_status{ status_l, last_votes, last_blocks, tally_impl () };
}

std::shared_ptr<lumex::block> lumex::election::winner () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return status.winner;
}

std::chrono::milliseconds lumex::election::duration () const
{
	return std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - election_start);
}

void lumex::election::broadcast_vote_locked (lumex::unique_lock<lumex::mutex> & lock)
{
	debug_assert (lock.owns_lock ());

	if (std::chrono::steady_clock::now () < last_vote + node.config.network_params.network.vote_broadcast_interval)
	{
		return;
	}
	last_vote = std::chrono::steady_clock::now ();

	if (node.config.enable_voting && node.wallets.reps ().voting > 0)
	{
		node.stats.inc (lumex::stat::type::election, lumex::stat::detail::broadcast_vote);
		++vote_broadcast_count;

		if (confirmed_locked () || have_quorum (tally_impl ()))
		{
			node.stats.inc (lumex::stat::type::election, lumex::stat::detail::broadcast_vote_final);
			node.logger.trace (lumex::log::type::election, lumex::log::detail::broadcast_vote,
			lumex::log::arg{ "id", id },
			lumex::log::arg{ "qualified_root", qualified_root },
			lumex::log::arg{ "winner", status.winner },
			lumex::log::arg{ "type", "final" });

			node.vote_generator.vote_final (qualified_root, status.winner->hash (), bucket);
		}
		else
		{
			node.stats.inc (lumex::stat::type::election, lumex::stat::detail::broadcast_vote_normal);
			node.logger.trace (lumex::log::type::election, lumex::log::detail::broadcast_vote,
			lumex::log::arg{ "id", id },
			lumex::log::arg{ "qualified_root", qualified_root },
			lumex::log::arg{ "winner", status.winner },
			lumex::log::arg{ "type", "normal" });

			node.vote_generator.vote_normal (qualified_root, status.winner->hash (), bucket);
		}
	}
}

void lumex::election::remove_votes (lumex::block_hash const & hash_a)
{
	debug_assert (!mutex.try_lock ());
	if (node.config.enable_voting && node.wallets.reps ().voting > 0)
	{
		// Remove votes from election
		auto list_generated_votes (node.history.votes (root, hash_a));
		for (auto const & vote : list_generated_votes)
		{
			last_votes.erase (vote->account);
		}
		// Clear votes cache
		node.history.erase (root);
	}
}

void lumex::election::remove_block (lumex::block_hash const & hash_a)
{
	debug_assert (!mutex.try_lock ());
	if (status.winner->hash () != hash_a)
	{
		if (auto existing = last_blocks.find (hash_a); existing != last_blocks.end ())
		{
			erase_if (last_votes, [hash_a] (auto const & entry) {
				return entry.second.hash == hash_a;
			});

			node.network.filter.clear (existing->second);
			last_blocks.erase (hash_a);
		}
	}
}

bool lumex::election::replace_by_weight (lumex::unique_lock<lumex::mutex> & lock_a, lumex::block_hash const & hash_a)
{
	debug_assert (lock_a.owns_lock ());
	lumex::block_hash replaced_block (0);
	auto winner_hash (status.winner->hash ());
	// Sort existing blocks tally
	std::vector<std::pair<lumex::block_hash, lumex::uint128_t>> sorted;
	sorted.reserve (last_tally.size ());
	std::copy (last_tally.begin (), last_tally.end (), std::back_inserter (sorted));
	lock_a.unlock ();

	// Sort in ascending order
	std::sort (sorted.begin (), sorted.end (), [] (auto const & left, auto const & right) { return left.second < right.second; });

	auto votes_tally = [this] (std::vector<std::shared_ptr<lumex::vote>> const & votes) {
		lumex::uint128_t result{ 0 };
		for (auto const & vote : votes)
		{
			result += node.ledger.weight (vote->account);
		}
		return result;
	};

	// Replace if lowest tally is below inactive cache new block weight
	auto inactive_existing = node.vote_cache.find (hash_a);
	auto inactive_tally = votes_tally (inactive_existing);
	if (inactive_tally > 0 && sorted.size () < max_blocks)
	{
		// If count of tally items is less than 10, remove any block without tally
		for (auto const & [hash, block] : blocks ())
		{
			if (std::find_if (sorted.begin (), sorted.end (), [&hash = hash] (auto const & item_a) { return item_a.first == hash; }) == sorted.end () && hash != winner_hash)
			{
				replaced_block = hash;
				break;
			}
		}
	}
	else if (inactive_tally > 0 && inactive_tally > sorted.front ().second)
	{
		if (sorted.front ().first != winner_hash)
		{
			replaced_block = sorted.front ().first;
		}
		else if (inactive_tally > sorted[1].second)
		{
			// Avoid removing winner
			replaced_block = sorted[1].first;
		}
	}

	bool replaced (false);
	if (!replaced_block.is_zero ())
	{
		node.vote_router.disconnect (replaced_block);
		lock_a.lock ();
		remove_block (replaced_block);
		replaced = true;
	}
	else
	{
		lock_a.lock ();
	}
	return replaced;
}

void lumex::election::force_confirm ()
{
	release_assert (node.network_params.network.is_dev_network ());
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	confirm_once (lock);
}

std::unordered_set<lumex::block_hash> lumex::election::blocks_hashes () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	std::unordered_set<lumex::block_hash> hashes;
	for (auto const & block : last_blocks)
	{
		hashes.emplace (block.first);
	}
	return hashes;
}

std::unordered_map<lumex::block_hash, std::shared_ptr<lumex::block>> lumex::election::blocks () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return last_blocks;
}

std::unordered_map<lumex::account, lumex::vote_info> lumex::election::votes () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return last_votes;
}

std::vector<lumex::vote_with_weight_info> lumex::election::votes_with_weight () const
{
	std::multimap<lumex::uint128_t, lumex::vote_with_weight_info, std::greater<lumex::uint128_t>> sorted_votes;
	std::vector<lumex::vote_with_weight_info> result;
	auto votes_l (votes ());
	for (auto const & vote_l : votes_l)
	{
		if (vote_l.first != nullptr)
		{
			auto amount (node.ledger.weight (vote_l.first));
			lumex::vote_with_weight_info vote_info{ vote_l.first, vote_l.second.time, vote_l.second.timestamp, vote_l.second.hash, amount };
			sorted_votes.emplace (std::move (amount), vote_info);
		}
	}
	result.reserve (sorted_votes.size ());
	std::transform (sorted_votes.begin (), sorted_votes.end (), std::back_inserter (result), [] (auto const & entry) { return entry.second; });
	return result;
}

lumex::election_behavior lumex::election::behavior () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return behavior_m;
}

lumex::election_state lumex::election::state () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return state_m;
}

bool lumex::election::contains (lumex::block_hash const & hash) const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return last_blocks.contains (hash);
}

size_t lumex::election::voter_count () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return last_votes.size ();
}

size_t lumex::election::block_count () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return last_blocks.size ();
}

void lumex::election::operator() (lumex::object_stream & obs) const
{
	obs.write ("id", id);
	obs.write ("qualified_root", qualified_root);
	obs.write ("behavior", behavior_m);
	obs.write ("height", height);
	obs.write ("status", current_status ());
}

void lumex::election_extended_status::operator() (lumex::object_stream & obs) const
{
	obs.write ("winner", status.winner->hash ());
	obs.write ("tally_amount", status.tally.to_string_dec ());
	obs.write ("final_tally_amount", status.final_tally.to_string_dec ());
	obs.write ("confirmation_request_count", status.confirmation_request_count);
	obs.write ("vote_broadcast_count", status.vote_broadcast_count);
	obs.write ("block_count", status.block_count);
	obs.write ("voter_count", status.voter_count);
	obs.write ("type", status.type);

	obs.write_range ("votes", votes, [] (auto const & entry, lumex::object_stream & obs) {
		auto & [account, info] = entry;
		obs.write ("account", account);
		obs.write ("hash", info.hash);
		obs.write ("final", lumex::vote::is_final_timestamp (info.timestamp));
		obs.write ("timestamp", info.timestamp);
		obs.write ("time", info.time.time_since_epoch ().count ());
	});

	obs.write_range ("blocks", blocks, [] (auto const & entry) {
		auto [hash, block] = entry;
		return block;
	});

	obs.write_range ("tally", tally, [] (auto const & entry, lumex::object_stream & obs) {
		auto & [amount, block] = entry;
		obs.write ("hash", block->hash ());
		obs.write ("amount", amount);
	});
}

/*
 *
 */

std::string_view lumex::to_string (lumex::election_behavior behavior)
{
	return lumex::enum_to_string (behavior);
}

lumex::stat::detail lumex::to_stat_detail (lumex::election_behavior behavior)
{
	return lumex::enum_convert<lumex::stat::detail> (behavior);
}

std::string_view lumex::to_string (lumex::election_state state)
{
	return lumex::enum_to_string (state);
}

lumex::stat::detail lumex::to_stat_detail (lumex::election_state state)
{
	return lumex::enum_convert<lumex::stat::detail> (state);
}
