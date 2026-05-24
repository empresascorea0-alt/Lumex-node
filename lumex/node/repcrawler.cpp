#include <lumex/lib/blocks.hpp>
#include <lumex/lib/formatting.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/vote.hpp>
#include <lumex/messages/confirm.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/node_observers.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/online_reps.hpp>
#include <lumex/node/repcrawler.hpp>
#include <lumex/node/transport/formatting.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_set_cemented.hpp>

#include <ranges>

lumex::rep_crawler::rep_crawler (lumex::rep_crawler_config const & config_a, lumex::node & node_a) :
	config{ config_a },
	node{ node_a },
	stats{ node_a.stats },
	logger{ node_a.logger },
	network_constants{ node_a.network_params.network },
	active{ node_a.active }
{
	node.observers.channel_connected.add ([this] (std::shared_ptr<lumex::transport::channel> const & channel) {
		if (!node.flags.disable_rep_crawler)
		{
			{
				lumex::lock_guard<lumex::mutex> lock{ mutex };
				prioritized.push_back (channel);
			}
			condition.notify_all ();
		}
	});
}

lumex::rep_crawler::~rep_crawler ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void lumex::rep_crawler::start ()
{
	debug_assert (!thread.joinable ());

	if (node.flags.disable_rep_crawler)
	{
		return;
	}

	thread = std::thread{ [this] () {
		lumex::thread_role::set (lumex::thread_role::name::rep_crawler);
		run ();
	} };
}

void lumex::rep_crawler::stop ()
{
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

// Exits with the lock unlocked
void lumex::rep_crawler::validate_and_process (lumex::unique_lock<lumex::mutex> & lock)
{
	debug_assert (!mutex.try_lock ());
	debug_assert (lock.owns_lock ());
	debug_assert (!responses.empty ()); // Should be checked before calling this function

	decltype (responses) responses_l{ responses.capacity () };
	responses_l.swap (responses);

	lock.unlock ();

	// normally the rep_crawler only tracks principal reps but it can be made to track
	// reps with less weight by setting rep_crawler_weight_minimum to a low value
	auto const minimum = std::min (node.minimum_principal_weight (), node.config.rep_crawler_weight_minimum.number ());

	// TODO: Is it really faster to repeatedly lock/unlock the mutex for each response?
	for (auto const & response : responses_l)
	{
		auto & vote = response.second;
		auto & channel = response.first;
		release_assert (vote != nullptr);
		release_assert (channel != nullptr);

		lumex::uint128_t const rep_weight = node.ledger.weight (vote->account);
		if (rep_weight < minimum)
		{
			logger.debug (lumex::log::type::rep_crawler, "Ignoring vote from account: {} with too little voting weight: {}",
			vote->account,
			rep_weight);

			continue; // Skip this vote
		}

		// temporary data used for logging after dropping the lock
		bool inserted = false;
		bool updated = false;
		std::shared_ptr<lumex::transport::channel> prev_channel;

		lock.lock ();

		if (auto existing = reps.find (vote->account); existing != reps.end ())
		{
			reps.modify (existing, [rep_weight, &updated, &vote, &channel, &prev_channel] (rep_entry & rep) {
				rep.last_response = std::chrono::steady_clock::now ();

				// Update if representative channel was changed
				if (rep.channel->get_remote_endpoint () != channel->get_remote_endpoint ())
				{
					debug_assert (rep.account == vote->account);
					updated = true;
					prev_channel = rep.channel;
					rep.channel = channel;
				}
			});
		}
		else
		{
			reps.emplace (rep_entry{ vote->account, channel });
			inserted = true;
		}

		lock.unlock ();

		if (inserted)
		{
			logger.info (lumex::log::type::rep_crawler, "Found representative: {} at: {}",
			vote->account,
			channel);
		}
		if (updated)
		{
			logger.warn (lumex::log::type::rep_crawler, "Updated representative: {} at: {} (was at: {})",
			vote->account,
			channel,
			prev_channel);
		}
	}
}

std::chrono::milliseconds lumex::rep_crawler::query_interval (bool sufficient_weight) const
{
	return sufficient_weight ? network_constants.rep_crawler_normal_interval : network_constants.rep_crawler_warmup_interval;
}

bool lumex::rep_crawler::query_predicate (bool sufficient_weight) const
{
	return lumex::elapsed (last_query, query_interval (sufficient_weight));
}

void lumex::rep_crawler::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		lock.unlock ();

		auto const current_total_weight = total_weight ();
		bool const sufficient_weight = current_total_weight > node.online_reps.delta ();

		// If online weight drops below minimum, reach out to preconfigured peers
		if (!sufficient_weight)
		{
			stats.inc (lumex::stat::type::rep_crawler, lumex::stat::detail::keepalive);
			node.network.trigger_reachout ();
		}

		lock.lock ();

		condition.wait_for (lock, query_interval (sufficient_weight), [this, sufficient_weight] {
			return stopped || query_predicate (sufficient_weight) || !responses.empty () || !prioritized.empty ();
		});

		if (stopped)
		{
			return;
		}

		stats.inc (lumex::stat::type::rep_crawler, lumex::stat::detail::loop);

		if (!responses.empty ())
		{
			validate_and_process (lock);
			debug_assert (!lock.owns_lock ());
			lock.lock ();
		}

		cleanup ();

		if (!prioritized.empty ())
		{
			decltype (prioritized) prioritized_l;
			prioritized_l.swap (prioritized);

			lock.unlock ();
			query (prioritized_l);
			lock.lock ();
		}

		if (query_predicate (sufficient_weight))
		{
			last_query = std::chrono::steady_clock::now ();

			auto targets = prepare_crawl_targets (sufficient_weight);

			lock.unlock ();
			query (targets);
			query (node.loopback_channel); // Query local representative
			lock.lock ();
		}

		debug_assert (lock.owns_lock ());
	}
}

void lumex::rep_crawler::cleanup ()
{
	debug_assert (!mutex.try_lock ());

	// Evict reps with dead channels
	erase_if (reps, [this] (rep_entry const & rep) {
		if (!rep.channel->alive ())
		{
			stats.inc (lumex::stat::type::rep_crawler, lumex::stat::detail::channel_dead);
			logger.info (lumex::log::type::rep_crawler, "Evicting representative: {} with dead channel at: {}", rep.account, rep.channel);
			return true; // Erase
		}
		return false;
	});

	// Evict queries that haven't been responded to in a while
	erase_if (queries, [this] (query_entry const & query) {
		if (lumex::elapsed (query.time, config.query_timeout))
		{
			if (query.replies == 0)
			{
				stats.inc (lumex::stat::type::rep_crawler, lumex::stat::detail::query_timeout);
				logger.debug (lumex::log::type::rep_crawler, "Aborting unresponsive query for block: {} from: {}", query.hash, query.channel);
			}
			else
			{
				stats.inc (lumex::stat::type::rep_crawler, lumex::stat::detail::query_completion);
				logger.debug (lumex::log::type::rep_crawler, "Completion of query with: {} replies for block: {} from: {}", query.replies, query.hash, query.channel);
			}
			return true; // Erase
		}
		return false;
	});
}

std::deque<std::shared_ptr<lumex::transport::channel>> lumex::rep_crawler::prepare_crawl_targets (bool sufficient_weight) const
{
	debug_assert (!mutex.try_lock ());

	// TODO: Make these values configurable
	constexpr std::size_t conservative_count = 160;
	constexpr std::size_t aggressive_count = 160;
	constexpr std::size_t conservative_max_attempts = 4;
	constexpr std::size_t aggressive_max_attempts = 8;
	std::chrono::milliseconds rep_query_interval = node.network_params.network.is_dev_network () ? std::chrono::milliseconds{ 500 } : std::chrono::milliseconds{ 60 * 1000 };

	stats.inc (lumex::stat::type::rep_crawler, sufficient_weight ? lumex::stat::detail::crawl_normal : lumex::stat::detail::crawl_aggressive);

	// Crawl more aggressively if we lack sufficient total peer weight.
	auto const required_peer_count = sufficient_weight ? conservative_count : aggressive_count;

	auto random_peers = node.network.random_set (required_peer_count);

	auto should_query = [&, this] (std::shared_ptr<lumex::transport::channel> const & channel) {
		if (auto rep = reps.get<tag_channel> ().find (channel); rep != reps.get<tag_channel> ().end ())
		{
			// Throttle queries to active reps
			return elapsed (rep->last_request, rep_query_interval);
		}
		else
		{
			// Avoid querying the same peer multiple times when rep crawler is warmed up
			auto const max_attempts = sufficient_weight ? conservative_max_attempts : aggressive_max_attempts;
			return queries.get<tag_channel> ().count (channel) < max_attempts;
		}
	};

	erase_if (random_peers, [&, this] (std::shared_ptr<lumex::transport::channel> const & channel) {
		return !should_query (channel);
	});

	return { random_peers.begin (), random_peers.end () };
}

auto lumex::rep_crawler::prepare_query_target () const -> hash_root_t
{
	constexpr int max_attempts = 32;

	auto transaction = node.ledger.tx_begin_read ();

	auto random_blocks = node.ledger.random_blocks (transaction, max_attempts);
	for (auto const & block : random_blocks)
	{
		// Avoid blocks that could still have live votes coming in
		if (active.recently_confirmed.contains (block->hash ()))
		{
			continue;
		}

		// Nodes will not respond to queries for blocks that are not confirmed
		if (!node.ledger.cemented.block_exists (transaction, block->hash ()))
		{
			continue;
		}

		return std::make_pair (block->hash (), block->root ());
	}

	// If no suitable block was found, query genesis
	return std::make_pair (node.network_params.ledger.genesis->hash (), node.network_params.ledger.genesis->root ());
}

bool lumex::rep_crawler::track_rep_request (hash_root_t hash_root, std::shared_ptr<lumex::transport::channel> const & channel)
{
	debug_assert (!mutex.try_lock ());

	auto [_, inserted] = queries.emplace (query_entry{ hash_root.first, channel });
	if (!inserted)
	{
		return false; // Duplicate, not tracked
	}

	// Find and update the timestamp on all reps available on the endpoint (a single host may have multiple reps)
	auto & index = reps.get<tag_channel> ();
	auto [begin, end] = index.equal_range (channel);
	for (auto it = begin; it != end; ++it)
	{
		index.modify (it, [] (rep_entry & info) {
			info.last_request = std::chrono::steady_clock::now ();
		});
	}

	return true;
}

void lumex::rep_crawler::query (std::deque<std::shared_ptr<lumex::transport::channel>> const & target_channels)
{
	auto hash_root = prepare_query_target ();

	lumex::unique_lock<lumex::mutex> lock{ mutex };

	for (const auto & channel : target_channels)
	{
		bool tracked = track_rep_request (hash_root, channel);
		if (tracked)
		{
			stats.inc (lumex::stat::type::rep_crawler, lumex::stat::detail::query_sent);
			logger.debug (lumex::log::type::rep_crawler, "Sending query for block: {} to: {}", hash_root.first, channel);

			lock.unlock ();

			auto const & [hash, root] = hash_root;
			lumex::messages::confirm_req req{ network_constants, hash, root };

			channel->send (req, lumex::transport::traffic_type::rep_crawler, [this] (auto & ec, auto size) {
				stats.inc (lumex::stat::type::rep_crawler_ec, to_stat_detail (ec), lumex::stat::dir::out);
			});

			lock.lock ();
		}
		else
		{
			stats.inc (lumex::stat::type::rep_crawler, lumex::stat::detail::query_duplicate);
			logger.debug (lumex::log::type::rep_crawler, "Ignoring duplicate query for block: {} to: {}", hash_root.first, channel);
		}
	}
}

void lumex::rep_crawler::query (std::shared_ptr<lumex::transport::channel> const & target_channel)
{
	query (std::deque{ target_channel });
}

bool lumex::rep_crawler::is_pr (std::shared_ptr<lumex::transport::channel> const & channel) const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	auto existing = reps.get<tag_channel> ().find (channel);
	if (existing != reps.get<tag_channel> ().end ())
	{
		return node.ledger.weight (existing->account) >= node.minimum_principal_weight ();
	}
	return false;
}

bool lumex::rep_crawler::process (std::shared_ptr<lumex::vote> const & vote, std::shared_ptr<lumex::transport::channel> const & channel)
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };

	auto [begin, end] = queries.get<tag_channel> ().equal_range (channel);
	for (auto it = begin; it != end; ++it)
	{
		// TODO: This linear search could be slow, especially with large votes.
		auto const target_hash = it->hash;
		bool found = std::any_of (vote->hashes.begin (), vote->hashes.end (), [&target_hash] (lumex::block_hash const & hash) {
			return hash == target_hash;
		});
		if (found)
		{
			stats.inc (lumex::stat::type::rep_crawler, lumex::stat::detail::response);
			logger.debug (lumex::log::type::rep_crawler, "Processing response for block: {} from: {}", target_hash, channel);

			// Track response time
			stats.sample (lumex::stat::sample::rep_response_time, lumex::log::milliseconds_delta (it->time), { 0, config.query_timeout.count () });

			responses.push_back ({ channel, vote });
			queries.modify (it, [] (query_entry & e) {
				e.replies++;
			});
			condition.notify_all ();
			return true; // Found and processed
		}
	}
	return false;
}

lumex::uint128_t lumex::rep_crawler::total_weight () const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	lumex::uint128_t result = 0;
	for (const auto & rep : reps)
	{
		if (rep.channel->alive ())
		{
			result += node.ledger.weight (rep.account);
		}
	}
	return result;
}

std::vector<lumex::representative> lumex::rep_crawler::representatives (std::size_t count, lumex::uint128_t const minimum_weight, std::optional<decltype (lumex::network_constants::protocol_version)> const & minimum_protocol_version) const
{
	auto const version_min = minimum_protocol_version.value_or (node.network_params.network.protocol_version_min);

	lumex::lock_guard<lumex::mutex> lock{ mutex };

	std::multimap<lumex::amount, rep_entry, std::greater<>> ordered;
	for (const auto & rep : reps.get<tag_account> ())
	{
		auto weight = node.ledger.weight (rep.account);
		if (weight >= minimum_weight && rep.channel->get_network_version () >= version_min)
		{
			ordered.insert ({ lumex::amount{ weight }, rep });
		}
	}

	std::vector<lumex::representative> result;
	result.reserve (ordered.size ());
	for (auto i = ordered.begin (), n = ordered.end (); i != n && result.size () < count; ++i)
	{
		auto const & [weight, rep] = *i;
		result.push_back ({ rep.account, rep.channel });
	}
	return result;
}

std::vector<lumex::representative> lumex::rep_crawler::principal_representatives (std::size_t count, std::optional<decltype (lumex::network_constants::protocol_version)> const & minimum_protocol_version) const
{
	return representatives (count, node.minimum_principal_weight (), minimum_protocol_version);
}

std::size_t lumex::rep_crawler::representative_count () const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	return reps.size ();
}

// Only for tests
void lumex::rep_crawler::force_add_rep (const lumex::account & account, const std::shared_ptr<lumex::transport::channel> & channel)
{
	release_assert (node.network_params.network.is_dev_network ());
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	reps.emplace (rep_entry{ account, channel });
}

// Only for tests
void lumex::rep_crawler::force_process (const std::shared_ptr<lumex::vote> & vote, const std::shared_ptr<lumex::transport::channel> & channel)
{
	release_assert (node.network_params.network.is_dev_network ());
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	responses.push_back ({ channel, vote });
}

// Only for tests
void lumex::rep_crawler::force_query (const lumex::block_hash & hash, const std::shared_ptr<lumex::transport::channel> & channel)
{
	release_assert (node.network_params.network.is_dev_network ());
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	queries.emplace (query_entry{ hash, channel });
}

lumex::container_info lumex::rep_crawler::container_info () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	lumex::container_info info;
	info.put ("reps", reps);
	info.put ("queries", queries);
	info.put ("responses", responses);
	info.put ("prioritized", prioritized);
	return info;
}

/*
 * rep_crawler_config
 */

lumex::rep_crawler_config::rep_crawler_config (lumex::network_constants const & network_constants)
{
	if (network_constants.is_dev_network ())
	{
		query_timeout = std::chrono::milliseconds{ 1000 };
	}
}

lumex::error lumex::rep_crawler_config::serialize (lumex::tomlconfig & toml) const
{
	toml.put ("query_timeout", query_timeout.count (), "Timeout for rep crawler queries.\ntype:milliseconds");

	return toml.get_error ();
}

lumex::error lumex::rep_crawler_config::deserialize (lumex::tomlconfig & toml)
{
	toml.get_duration ("query_timeout", query_timeout);

	return toml.get_error ();
}
