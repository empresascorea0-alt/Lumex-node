#include <lumex/lib/assert.hpp>
#include <lumex/lib/interval.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/lib/saturate.hpp>
#include <lumex/lib/thread_roles.hpp>
#include <lumex/lib/vote.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/rep_tiers.hpp>
#include <lumex/node/vote_processor.hpp>
#include <lumex/node/vote_rebroadcaster.hpp>
#include <lumex/node/vote_router.hpp>
#include <lumex/node/wallet.hpp>
#include <lumex/secure/ledger.hpp>

lumex::vote_rebroadcaster::vote_rebroadcaster (lumex::vote_rebroadcaster_config const & config_a, lumex::node_flags const & flags_a, lumex::ledger & ledger_a, lumex::vote_router & vote_router_a, lumex::network & network_a, lumex::wallet::wallets & wallets_a, lumex::rep_tiers & rep_tiers_a, lumex::stats & stats_a, lumex::logger & logger_a) :
	config{ config_a },
	flags{ flags_a },
	ledger{ ledger_a },
	vote_router{ vote_router_a },
	network{ network_a },
	wallets{ wallets_a },
	rep_tiers{ rep_tiers_a },
	stats{ stats_a },
	logger{ logger_a },
	rebroadcasts{ config }
{
	if (!config.enable)
	{
		return;
	}

	queue.max_size_query = [this] (auto const & origin) {
		switch (origin.source)
		{
			case lumex::rep_tier::tier_3:
			case lumex::rep_tier::tier_2:
			case lumex::rep_tier::tier_1:
				return config.max_queue;
			case lumex::rep_tier::none:
				return size_t{ 0 };
		}
		debug_assert (false);
		return size_t{ 0 };
	};

	queue.priority_query = [this] (auto const & origin) {
		switch (origin.source)
		{
			case lumex::rep_tier::tier_3:
				return config.priority_coefficient * config.priority_coefficient * config.priority_coefficient;
			case lumex::rep_tier::tier_2:
				return config.priority_coefficient * config.priority_coefficient;
			case lumex::rep_tier::tier_1:
				return config.priority_coefficient;
			case lumex::rep_tier::none:
				return size_t{ 0 };
		}
		debug_assert (false);
		return size_t{ 0 };
	};

	vote_router.vote_processed.add ([this] (std::shared_ptr<lumex::vote> const & vote, lumex::vote_source source, std::unordered_map<lumex::block_hash, lumex::vote_code> const & results) {
		// We also want to allow late votes to be rebroadcasted to help with reaching quorum for other nodes
		bool should_rebroadcast = std::any_of (results.begin (), results.end (), [&] (auto const & result) {
			auto const code = result.second;
			if (code == lumex::vote_code::vote)
			{
				return true; // Rebroadcast votes that were processed by active elections
			}
			if (code != lumex::vote_code::indeterminate)
			{
				return vote->is_final (); // Rebroadcast late votes only if they are final
			}
			return false;
		});

		// Enable vote rebroadcasting only if the node does not host a representative (or super_rebroadcaster mode)
		// Do not rebroadcast votes from non-principal representatives
		if (should_rebroadcast && (!has_principal || flags.super_rebroadcaster))
		{
			auto tier = rep_tiers.tier (vote->account);
			if (tier != lumex::rep_tier::none)
			{
				push (vote, tier);
			}
		}
	});
}

lumex::vote_rebroadcaster::~vote_rebroadcaster ()
{
	debug_assert (!thread.joinable ());
}

void lumex::vote_rebroadcaster::start ()
{
	debug_assert (!thread.joinable ());

	if (!config.enable)
	{
		return;
	}

	thread = std::thread ([this] () {
		lumex::thread_role::set (lumex::thread_role::name::vote_rebroadcasting);
		run ();
	});
}

void lumex::vote_rebroadcaster::stop ()
{
	{
		std::lock_guard guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

bool lumex::vote_rebroadcaster::push (std::shared_ptr<lumex::vote> const & vote, lumex::rep_tier tier)
{
	bool added = false;
	{
		std::lock_guard guard{ mutex };

		// Do not rebroadcast local representative votes
		if (!reps.exists (vote->account) && !queue_hashes.contains (vote->signature))
		{
			added = queue.push (vote, tier);
			if (added)
			{
				queue_hashes.insert (vote->signature); // Keep track of vote signatures to avoid duplicates
			}
		}
	}
	if (added)
	{
		stats.inc (lumex::stat::type::vote_rebroadcaster, lumex::stat::detail::queued);
		condition.notify_one ();
	}
	return added;
}

std::pair<std::shared_ptr<lumex::vote>, lumex::rep_tier> lumex::vote_rebroadcaster::next ()
{
	debug_assert (!mutex.try_lock ());
	debug_assert (!queue.empty ());

	queue.periodic_update ();

	auto [vote, origin] = queue.next ();
	release_assert (vote != nullptr);
	release_assert (origin.source != lumex::rep_tier::none);

	auto erased = queue_hashes.erase (vote->signature);
	debug_assert (erased == 1);

	return { vote, origin.source };
}

void lumex::vote_rebroadcaster::run ()
{
	std::unique_lock lock{ mutex };
	while (!stopped)
	{
		// Should be woken up periodically to perform maintenance tasks even if no votes are queued
		condition.wait_for (lock, 1s, [&] {
			return stopped || !queue.empty ();
		});

		if (stopped)
		{
			return;
		}

		stats.inc (lumex::stat::type::vote_rebroadcaster, lumex::stat::detail::loop);

		// Update local reps cache
		if (refresh_interval.elapse (lumex::is_dev_run () ? 1s : 15s))
		{
			// Check if node has a principal representative (rebroadcasting is disabled when true)
			reps = wallets.reps ();
			has_principal = reps.have_half_rep ();

			stats.inc (lumex::stat::type::vote_rebroadcaster, lumex::stat::detail::refresh);
		}

		// Cleanup expired representatives from rebroadcasts
		if (cleanup_interval.elapse (lumex::is_dev_run () ? 1s : 60s))
		{
			lock.unlock ();
			cleanup ();
			lock.lock ();
		}

		if (queue.empty ())
		{
			continue; // Nothing to process
		}

		// Wait for spare capacity if our network traffic is too high
		if (!check_capacity ())
		{
			stats.inc (lumex::stat::type::vote_rebroadcaster, lumex::stat::detail::cooldown);

			if (!queue.empty () && capacity_warning_interval.elapse (5s))
			{
				logger.warn (lumex::log::type::vote_rebroadcaster, "Network capacity for vote rebroadcasting unavailable, {} votes waiting in queue", queue.size ());
			}

			lock.unlock ();
			std::this_thread::sleep_for (100ms);
			lock.lock ();
			continue; // Wait for more capacity
		}

		if (!queue.empty ())
		{
			// Only log if component is under pressure
			if (queue.size () > lumex::queue_warning_threshold () && log_interval.elapse (15s))
			{
				logger.info (lumex::log::type::vote_rebroadcaster, "{} votes (tier 3: {}, tier 2: {}, tier 1: {}) in rebroadcast queue",
				queue.size (),
				queue.size ({ lumex::rep_tier::tier_3 }),
				queue.size ({ lumex::rep_tier::tier_2 }),
				queue.size ({ lumex::rep_tier::tier_1 }));
			}

			auto [vote, tier] = next ();

			lock.unlock ();

			bool should_rebroadcast = process (vote);
			if (should_rebroadcast)
			{
				stats.inc (lumex::stat::type::vote_rebroadcaster, lumex::stat::detail::rebroadcast);
				stats.add (lumex::stat::type::vote_rebroadcaster, lumex::stat::detail::rebroadcast_hashes, vote->hashes.size ());
				stats.inc (lumex::stat::type::vote_rebroadcaster_tier, to_stat_detail (tier));

				auto sent = broadcast (vote);
				stats.add (lumex::stat::type::vote_rebroadcaster, lumex::stat::detail::sent, sent);
			}

			lock.lock ();
		}
	}
}

size_t lumex::vote_rebroadcaster::broadcast (std::shared_ptr<lumex::vote> const & vote)
{
	if (flags.super_rebroadcaster)
	{
		stats.inc (lumex::stat::type::vote_rebroadcaster, lumex::stat::detail::broadcast_super);
		return network.flood_vote_all (vote, lumex::transport::traffic_type::vote_rebroadcast, true /* rebroadcasted */);
	}
	else
	{
		stats.inc (lumex::stat::type::vote_rebroadcaster, lumex::stat::detail::broadcast);
		return network.flood_vote (vote, lumex::transport::traffic_type::vote_rebroadcast, true /* rebroadcasted */);
	}
}

bool lumex::vote_rebroadcaster::check_capacity () const
{
	if (flags.super_rebroadcaster)
	{
		return network.check_capacity_ratio (lumex::transport::traffic_type::vote_rebroadcast, 0.5f);
	}
	else
	{
		return network.check_capacity_fanout (lumex::transport::traffic_type::vote_rebroadcast);
	}
}

bool lumex::vote_rebroadcaster::process (std::shared_ptr<lumex::vote> const & vote)
{
	stats.inc (lumex::stat::type::vote_rebroadcaster, lumex::stat::detail::process);

	auto result = rebroadcasts->check_and_record (vote, ledger.weight (vote->account), std::chrono::steady_clock::now ());

	stats.inc (lumex::stat::type::vote_rebroadcaster_process, to_stat_detail (result));
	if (result == lumex::vote_rebroadcaster_index::result::ok)
	{
		return true; // Vote qualifies for rebroadcast
	}
	else
	{
		return false; // Vote does not qualify for rebroadcast
	}
}

void lumex::vote_rebroadcaster::cleanup ()
{
	stats.inc (lumex::stat::type::vote_rebroadcaster, lumex::stat::detail::cleanup);

	auto erased_reps = rebroadcasts->cleanup ([this] (auto const & rep) {
		auto tier = rep_tiers.tier (rep);
		auto weight = ledger.weight (rep);
		return std::make_pair (tier != lumex::rep_tier::none /* keep entry only if principal rep */, weight);
	});

	stats.add (lumex::stat::type::vote_rebroadcaster, lumex::stat::detail::representatives_erase_stale, erased_reps);
}

lumex::container_info lumex::vote_rebroadcaster::container_info () const
{
	std::lock_guard guard{ mutex };

	auto rebroadcasts_l = rebroadcasts.lock ();

	lumex::container_info info;
	info.add ("queue", queue.container_info ());
	info.put ("queue_total", queue.size ());
	info.put ("queue_hashes", queue_hashes.size ());
	info.put ("representatives", rebroadcasts_l->representatives_count ());
	info.put ("history", rebroadcasts_l->total_history ());
	info.put ("hashes", rebroadcasts_l->total_hashes ());
	return info;
}

/*
 * vote_rebroadcaster_index
 */

lumex::vote_rebroadcaster_index::vote_rebroadcaster_index (lumex::vote_rebroadcaster_config const & config_a) :
	config{ config_a }
{
}

lumex::vote_rebroadcaster_index::result lumex::vote_rebroadcaster_index::check_and_record (std::shared_ptr<lumex::vote> const & vote, lumex::uint128_t rep_weight, std::chrono::steady_clock::time_point now)
{
	auto const vote_timestamp = vote->timestamp ();
	auto const vote_hash = vote->full_hash ();

	auto it = index.get<tag_account> ().find (vote->account);

	// If we don't have a record for this rep, add it
	if (it == index.get<tag_account> ().end ())
	{
		auto should_add = [&, this] () {
			// Under normal conditions the number of principal representatives should be below this limit
			if (index.size () < config.max_representatives)
			{
				return true;
			}
			// However, if we're at capacity, we can still add the rep if it has a higher weight than the lowest weight in the container
			if (auto lowest = index.get<tag_weight> ().begin (); lowest != index.get<tag_weight> ().end ())
			{
				return rep_weight > lowest->weight;
			}
			return false;
		};

		if (should_add ())
		{
			it = index.get<tag_account> ().emplace (representative_entry{ vote->account, rep_weight }).first;
		}
		else
		{
			return result::representatives_full;
		}
	}
	release_assert (it != index.get<tag_account> ().end ());

	auto & history = it->history;
	auto & hashes = it->hashes;

	// Check if we already rebroadcasted this exact vote (fast lookup by hash)
	if (hashes.get<tag_vote_hash> ().contains (vote_hash))
	{
		return result::already_rebroadcasted;
	}

	// Check if any of the hashes contained in the vote qualifies for rebroadcasting
	auto check_hash = [&] (auto const & hash) {
		if (auto existing = history.get<tag_block_hash> ().find (hash); existing != history.get<tag_block_hash> ().end ())
		{
			// Always rebroadcast vote if rep switched to a final vote
			if (lumex::vote::is_final_timestamp (vote_timestamp) && vote_timestamp > existing->vote_timestamp)
			{
				return true;
			}
			// Otherwise only rebroadcast if sufficient time has passed since the last rebroadcast
			if (existing->timestamp + config.rebroadcast_threshold > now)
			{
				return false; // Not enough (as seen by local clock) time has passed
			}
			if (add_sat (existing->vote_timestamp, static_cast<lumex::vote_timestamp> (config.rebroadcast_threshold.count ())) > vote_timestamp)
			{
				return false; // Not enough (as seen by vote timestamp) time has passed
			}
			return true; // Enough time has passed, block hash qualifies for rebroadcast
		}
		else
		{
			return true; // Block hash not seen before, rebroadcast
		}
	};

	bool should_rebroadcast = std::any_of (vote->hashes.begin (), vote->hashes.end (), check_hash);
	if (!should_rebroadcast)
	{
		return result::rebroadcast_unnecessary;
	}

	// Update the history with the new vote info
	for (auto const & hash : vote->hashes)
	{
		if (auto existing = history.get<tag_block_hash> ().find (hash); existing != history.get<tag_block_hash> ().end ())
		{
			history.get<tag_block_hash> ().modify (existing, [&] (auto & entry) {
				entry.vote_timestamp = vote_timestamp;
				entry.timestamp = now;
			});
		}
		else
		{
			history.get<tag_block_hash> ().emplace (rebroadcast_entry{ hash, vote_timestamp, now });
		}
	}

	// Also keep track of the vote hash to quickly filter out duplicates
	hashes.push_back (vote_hash);

	// Keep history and hashes sizes within limits, erase oldest entries
	while (history.size () > config.max_history)
	{
		history.pop_front (); // Remove the oldest entry
	}
	while (hashes.size () > config.max_history)
	{
		hashes.pop_front (); // Remove the oldest entry
	}

	// Keep representatives index within limits, erase lowest weight entries
	while (!index.empty () && index.size () > config.max_representatives)
	{
		index.get<tag_weight> ().erase (index.get<tag_weight> ().begin ());
	}

	return result::ok; // Rebroadcast the vote
}

size_t lumex::vote_rebroadcaster_index::cleanup (rep_query query)
{
	// Remove entries for accounts that are no longer principal representatives
	auto erased_reps = erase_if (index, [&] (auto const & entry) {
		auto [should_keep, weight] = query (entry.representative);
		return !should_keep;
	});

	// Update representative weights
	for (auto it = index.begin (), end = index.end (); it != end; ++it)
	{
		index.modify (it, [&] (auto & entry) {
			auto [tier, weight] = query (entry.representative);
			entry.weight = weight;
		});
	}

	return erased_reps;
}

bool lumex::vote_rebroadcaster_index::contains_vote (lumex::block_hash const & vote_hash) const
{
	return std::any_of (index.begin (), index.end (), [&] (auto const & entry) {
		return entry.hashes.template get<tag_vote_hash> ().contains (vote_hash);
	});
}

bool lumex::vote_rebroadcaster_index::contains_representative (lumex::account const & representative) const
{
	return index.get<tag_account> ().contains (representative);
}

bool lumex::vote_rebroadcaster_index::contains_block (lumex::account const & representative, lumex::block_hash const & block_hash) const
{
	if (auto it = index.get<tag_account> ().find (representative); it != index.get<tag_account> ().end ())
	{
		return it->history.get<tag_block_hash> ().find (block_hash) != it->history.get<tag_block_hash> ().end ();
	}
	return false;
}

size_t lumex::vote_rebroadcaster_index::representatives_count () const
{
	return index.size ();
}

size_t lumex::vote_rebroadcaster_index::total_history () const
{
	return std::accumulate (index.begin (), index.end (), size_t{ 0 }, [] (auto total, auto const & entry) {
		return total + entry.history.size ();
	});
}

size_t lumex::vote_rebroadcaster_index::total_hashes () const
{
	return std::accumulate (index.begin (), index.end (), size_t{ 0 }, [] (auto total, auto const & entry) {
		return total + entry.hashes.size ();
	});
}

/*
 * vote_rebroadcaster_config
 */

lumex::error lumex::vote_rebroadcaster_config::deserialize (lumex::tomlconfig & toml)
{
	toml.get ("enable", enable);
	toml.get ("max_queue", max_queue);
	toml.get ("max_history", max_history);
	toml.get ("max_representatives", max_representatives);
	toml.get_duration ("rebroadcast_threshold", rebroadcast_threshold);
	toml.get ("priority_coefficient", priority_coefficient);

	return toml.get_error ();
}

lumex::error lumex::vote_rebroadcaster_config::serialize (lumex::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Enable or disable vote rebroadcasting. Disabling it will reduce bandwidth usage but should be done with understanding that the node will not participate fully in network consensus.\ntype:bool");
	toml.put ("max_queue", max_queue, "Maximum number of votes to keep in queue for processing.\ntype:uint64");
	toml.put ("max_history", max_history, "Maximum number of recently broadcast hashes to keep per representative.\ntype:uint64");
	toml.put ("max_representatives", max_representatives, "Maximum number of representatives to track rebroadcasts for.\ntype:uint64");
	toml.put ("rebroadcast_threshold", rebroadcast_threshold.count (), "Minimum amount of time between rebroadcasts for the same hash from the same representative.\ntype:milliseconds");
	toml.put ("priority_coefficient", priority_coefficient, "Priority coefficient for prioritizing votes from representative tiers.\ntype:uint64");

	return toml.get_error ();
}

/*
 *
 */

lumex::stat::detail lumex::to_stat_detail (lumex::vote_rebroadcaster_index::result result)
{
	return lumex::enum_convert<lumex::stat::detail> (result);
}