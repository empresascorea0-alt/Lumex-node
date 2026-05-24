#include <lumex/lib/blocks.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/lib/vote.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/transport/inproc.hpp>
#include <lumex/node/vote_generator.hpp>
#include <lumex/node/vote_processor.hpp>
#include <lumex/node/wallet.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_set_any.hpp>
#include <lumex/secure/voting_policy.hpp>

#include <chrono>

/*
 * vote_generator
 */

lumex::vote_generator::vote_generator (vote_generator_config const & config_a, lumex::voting_policy & policy_a, lumex::ledger & ledger_a, lumex::wallet::wallets & wallets_a, lumex::vote_processor & vote_processor_a, lumex::network & network_a, lumex::stats & stats_a, lumex::logger & logger_a, std::shared_ptr<lumex::transport::channel> inproc_channel_a) :
	config{ config_a },
	policy{ policy_a },
	ledger{ ledger_a },
	wallets{ wallets_a },
	vote_processor{ vote_processor_a },
	network{ network_a },
	stats{ stats_a },
	logger{ logger_a },
	inproc_channel{ inproc_channel_a },
	normal_verifier{ config_a.max_queue, config_a.batch_size, config_a.normal_threads, lumex::thread_role::name::voting_normal_processing },
	normal_broadcaster{ config_a.max_queue, lumex::network::confirm_ack_hashes_max, config_a.delay, lumex::thread_role::name::voting_normal_broadcast },
	final_verifier{ config_a.max_queue, config_a.batch_size, /* single threaded */ 1, lumex::thread_role::name::voting_final_processing },
	final_broadcaster{ config_a.max_queue, lumex::network::confirm_ack_hashes_max, config_a.delay, lumex::thread_role::name::voting_final_broadcast }
{
	normal_verifier.process_batch = [this] (auto batch) { process_normal (std::move (batch)); };
	final_verifier.process_batch = [this] (auto batch) { process_final (std::move (batch)); };
	normal_broadcaster.broadcast_batch = [this] (auto batch) { broadcast_normal (std::move (batch)); };
	final_broadcaster.broadcast_batch = [this] (auto batch) { broadcast_final (std::move (batch)); };
	normal_broadcaster.check_capacity = [this] () { return check_normal_capacity (); };
	final_broadcaster.check_capacity = [this] () { return check_final_capacity (); };
}

lumex::vote_generator::~vote_generator () = default;

void lumex::vote_generator::start ()
{
	normal_verifier.start ();
	final_verifier.start ();
	normal_broadcaster.start ();
	final_broadcaster.start ();
}

void lumex::vote_generator::stop ()
{
	normal_verifier.stop ();
	final_verifier.stop ();
	normal_broadcaster.stop ();
	final_broadcaster.stop ();
}

void lumex::vote_generator::vote (lumex::qualified_root const & root, lumex::block_hash const & hash, lumex::bucket_index bucket, lumex::vote_type type)
{
	switch (type)
	{
		case lumex::vote_type::normal:
			vote_normal (root, hash, bucket);
			break;
		case lumex::vote_type::final:
			vote_final (root, hash, bucket);
			break;
	}
}

void lumex::vote_generator::vote_normal (lumex::qualified_root const & root, lumex::block_hash const & hash, lumex::bucket_index bucket)
{
	if (normal_verifier.push (root, hash, bucket))
	{
		stats.inc (lumex::stat::type::vote_generator, lumex::stat::detail::queue);
	}
	else
	{
		stats.inc (lumex::stat::type::vote_generator, lumex::stat::detail::overfill);
	}
}

void lumex::vote_generator::vote_final (lumex::qualified_root const & root, lumex::block_hash const & hash, lumex::bucket_index bucket)
{
	if (final_verifier.push (root, hash, bucket))
	{
		stats.inc (lumex::stat::type::vote_generator_final, lumex::stat::detail::queue);
	}
	else
	{
		stats.inc (lumex::stat::type::vote_generator_final, lumex::stat::detail::overfill);
	}
}

void lumex::vote_generator::process_normal (std::deque<vote_generator_verifier::entry> batch)
{
	std::deque<lumex::vote_permit> verified;

	auto transaction = ledger.tx_begin_read ();

	for (auto const & [root, hash] : batch)
	{
		transaction.refresh_if_needed ();

		auto block = ledger.any.block_get (transaction, hash);
		if (block)
		{
			if (auto permit = policy.vote (transaction, *block))
			{
				verified.push_back (*permit);
			}
		}
	}

	for (auto const & permit : verified)
	{
		// Normal vote policy may upgrade a permit to final if a final vote was already recorded for this root
		switch (permit.type ())
		{
			case lumex::vote_type::normal:
				normal_broadcaster.push (permit.qualified_root (), permit);
				break;
			case lumex::vote_type::final:
				final_broadcaster.push (permit.qualified_root (), permit);
				break;
		}
	}
}

void lumex::vote_generator::process_final (std::deque<vote_generator_verifier::entry> batch)
{
	std::deque<lumex::vote_permit> verified;

	auto transaction = ledger.tx_begin_write (lumex::store::writer::voting_final);

	for (auto const & [root, hash] : batch)
	{
		transaction.refresh_if_needed ();

		auto block = ledger.any.block_get (transaction, hash);
		if (block)
		{
			if (auto permit = policy.vote_final (transaction, *block))
			{
				verified.push_back (*permit);
			}
		}
	}

	for (auto const & permit : verified)
	{
		debug_assert (permit.type () == lumex::vote_type::final);
		final_broadcaster.push (permit.qualified_root (), permit);
	}
}

void lumex::vote_generator::broadcast_normal (std::vector<lumex::vote_permit> permits)
{
	debug_assert (permits.size () <= lumex::network::confirm_ack_hashes_max);

	auto votes = policy.sign (lumex::vote_type::normal, permits, wallets.signer ());
	for (auto const & vote : votes)
	{
		stats.inc (lumex::stat::type::vote_generator, lumex::stat::detail::broadcast);
		stats.sample (lumex::stat::sample::vote_generator_hashes, vote->hashes.size (), { 0, lumex::network::confirm_ack_hashes_max });

		broadcast_vote (vote);
	}
}

void lumex::vote_generator::broadcast_final (std::vector<lumex::vote_permit> permits)
{
	debug_assert (permits.size () <= lumex::network::confirm_ack_hashes_max);

	auto votes = policy.sign (lumex::vote_type::final, permits, wallets.signer ());
	for (auto const & vote : votes)
	{
		stats.inc (lumex::stat::type::vote_generator_final, lumex::stat::detail::broadcast);
		stats.sample (lumex::stat::sample::vote_generator_final_hashes, vote->hashes.size (), { 0, lumex::network::confirm_ack_hashes_max });

		broadcast_vote (vote);
	}
}

bool lumex::vote_generator::check_normal_capacity ()
{
	bool ok = network.check_capacity_fanout (lumex::transport::traffic_type::vote_normal);
	if (!ok)
	{
		stats.inc (lumex::stat::type::vote_generator, lumex::stat::detail::cooldown);
		if (normal_capacity_log_interval.elapse (15s))
		{
			logger.warn (lumex::log::type::vote_generator, "Network capacity for vote broadcasting unavailable, {} normal / {} final votes pending",
			normal_broadcaster.size (), final_broadcaster.size ());
		}
	}
	return ok;
}

bool lumex::vote_generator::check_final_capacity ()
{
	bool ok = network.check_capacity_fanout (lumex::transport::traffic_type::vote_final);
	if (!ok)
	{
		stats.inc (lumex::stat::type::vote_generator_final, lumex::stat::detail::cooldown);
		if (final_capacity_log_interval.elapse (15s))
		{
			logger.warn (lumex::log::type::vote_generator_final, "Network capacity for final vote broadcasting unavailable, {} normal / {} final votes pending",
			normal_broadcaster.size (), final_broadcaster.size ());
		}
	}
	return ok;
}

void lumex::vote_generator::broadcast_vote (std::shared_ptr<lumex::vote> const & vote) const
{
	bool const is_final = vote->is_final ();
	auto const traffic_type = is_final ? lumex::transport::traffic_type::vote_final : lumex::transport::traffic_type::vote_normal;
	auto const stat_type = is_final ? lumex::stat::type::vote_generator_final : lumex::stat::type::vote_generator;

	vote_processor.vote (vote, inproc_channel);

	auto sent_pr = network.flood_vote_pr (vote, traffic_type);
	auto sent_non_pr = network.flood_vote_non_pr (vote, 2.0f, traffic_type);

	stats.add (stat_type, lumex::stat::detail::sent_pr, sent_pr);
	stats.add (stat_type, lumex::stat::detail::sent_non_pr, sent_non_pr);
}

lumex::container_info lumex::vote_generator::container_info () const
{
	lumex::container_info info;
	info.put ("normal_verifier", normal_verifier.size ());
	info.put ("normal_broadcaster", normal_broadcaster.size ());
	info.put ("final_verifier", final_verifier.size ());
	info.put ("final_broadcaster", final_broadcaster.size ());
	return info;
}

/*
 * vote_generator_config
 */

lumex::error lumex::vote_generator_config::serialize (lumex::tomlconfig & toml) const
{
	toml.put ("max_queue", max_queue, "Maximum number of entries in the vote generation queue. \ntype:uint64");
	toml.put ("batch_size", batch_size, "Maximum number of entries to process in a single batch. \ntype:uint64");
	toml.put ("normal_threads", normal_threads, "Number of threads for normal vote processing. \ntype:uint64");
	toml.put ("delay", delay.count (), "Delay before votes are sent to allow for efficient bundling of hashes in votes. \ntype:milliseconds");

	return toml.get_error ();
}

lumex::error lumex::vote_generator_config::deserialize (lumex::tomlconfig & toml)
{
	toml.get ("max_queue", max_queue);
	toml.get ("batch_size", batch_size);
	toml.get ("normal_threads", normal_threads);
	toml.get_duration ("delay", delay);

	return toml.get_error ();
}

/*
 * vote_generator_verifier
 */

lumex::vote_generator_verifier::vote_generator_verifier (size_t max_size_per_bucket, size_t batch_size, size_t thread_count, lumex::thread_role::name thread_role) :
	batch_size{ batch_size },
	thread_count{ thread_count },
	thread_role{ thread_role },
	index{ max_size_per_bucket }
{
}

lumex::vote_generator_verifier::~vote_generator_verifier ()
{
	debug_assert (threads.empty ());
}

void lumex::vote_generator_verifier::start ()
{
	debug_assert (process_batch);
	for (size_t i = 0; i < thread_count; ++i)
	{
		threads.emplace_back ([this] () {
			lumex::thread_role::set (thread_role);
			run ();
		});
	}
}

void lumex::vote_generator_verifier::stop ()
{
	{
		lumex::unique_lock<lumex::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	for (auto & thread : threads)
	{
		if (thread.joinable ())
		{
			thread.join ();
		}
	}
	threads.clear ();
}

bool lumex::vote_generator_verifier::push (lumex::qualified_root const & root, lumex::block_hash const & hash, lumex::bucket_index bucket)
{
	bool added = false;
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		added = index.push (root, hash, bucket);
	}
	if (added)
	{
		condition.notify_one ();
	}
	return added;
}

void lumex::vote_generator_verifier::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait (lock, [this] () {
			return stopped || !index.empty ();
		});
		if (stopped)
		{
			return;
		}

		auto batch = index.next_batch (batch_size);
		lock.unlock ();

		if (!batch.empty ())
		{
			process_batch (std::move (batch));
		}

		lock.lock ();
	}
}

size_t lumex::vote_generator_verifier::size () const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	return index.size ();
}

bool lumex::vote_generator_verifier::empty () const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	return index.empty ();
}

/*
 * vote_generator_broadcaster
 */

lumex::vote_generator_broadcaster::vote_generator_broadcaster (size_t max_size, size_t batch_threshold, std::chrono::milliseconds delay, lumex::thread_role::name thread_role) :
	batch_threshold{ batch_threshold },
	delay{ delay },
	thread_role{ thread_role },
	index{ max_size }
{
}

lumex::vote_generator_broadcaster::~vote_generator_broadcaster ()
{
	debug_assert (stopped);
}

void lumex::vote_generator_broadcaster::start ()
{
	debug_assert (broadcast_batch);
	debug_assert (check_capacity);
	thread = std::thread ([this] () {
		lumex::thread_role::set (thread_role);
		run ();
	});
}

void lumex::vote_generator_broadcaster::stop ()
{
	{
		lumex::unique_lock<lumex::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

bool lumex::vote_generator_broadcaster::push (lumex::qualified_root const & root, lumex::vote_permit const & permit)
{
	bool added = false;
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		added = index.push (root, permit);
	}
	if (added)
	{
		condition.notify_all ();
	}
	return added;
}

void lumex::vote_generator_broadcaster::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait_for (lock, delay / 2, [this] () {
			return stopped || predicate ();
		});
		if (stopped)
		{
			return;
		}
		if (predicate ())
		{
			// Wait for spare network capacity before broadcasting
			if (!check_capacity ())
			{
				lock.unlock ();
				std::this_thread::sleep_for (100ms);
				lock.lock ();
				continue;
			}

			auto batch = index.next_batch (batch_threshold);
			lock.unlock ();

			bool broadcasted = false;
			if (!batch.empty ())
			{
				broadcast_batch (std::move (batch));
				broadcasted = true;
			}

			lock.lock ();

			if (broadcasted)
			{
				last_broadcast = std::chrono::steady_clock::now ();
			}
		}
	}
}

bool lumex::vote_generator_broadcaster::predicate () const
{
	if (index.size () >= batch_threshold)
	{
		return true;
	}
	if (!index.empty () && std::chrono::steady_clock::now () - last_broadcast >= delay)
	{
		return true;
	}
	return false;
}

size_t lumex::vote_generator_broadcaster::size () const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	return index.size ();
}

bool lumex::vote_generator_broadcaster::empty () const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	return index.empty ();
}

/*
 * vote_broadcast_index
 */

lumex::vote_broadcast_index::vote_broadcast_index (size_t max_size) :
	max_size{ max_size }
{
}

bool lumex::vote_broadcast_index::push (lumex::qualified_root const & root, lumex::vote_permit permit)
{
	auto & by_root = entries.get<tag_root> ();
	if (auto existing = by_root.find (root); existing != by_root.end ())
	{
		if (existing->permit.hash () == permit.hash ())
		{
			return false; // Duplicate
		}
		// Conflict: different hash for same root, replace old entry
		by_root.erase (existing);
	}
	if (entries.size () >= max_size)
	{
		return false;
	}
	auto & sequenced = entries.get<tag_sequenced> ();
	auto [it, inserted] = sequenced.push_back ({ root, permit });
	return inserted;
}

bool lumex::vote_broadcast_index::erase (lumex::qualified_root const & root)
{
	auto & by_root = entries.get<tag_root> ();
	return by_root.erase (root) > 0;
}

std::vector<lumex::vote_permit> lumex::vote_broadcast_index::next_batch (size_t count)
{
	std::vector<lumex::vote_permit> batch;
	batch.reserve (count);

	auto & sequenced = entries.get<tag_sequenced> ();
	while (!sequenced.empty () && batch.size () < count)
	{
		batch.push_back (sequenced.front ().permit);
		sequenced.pop_front ();
	}

	return batch;
}

size_t lumex::vote_broadcast_index::size () const
{
	return entries.size ();
}

bool lumex::vote_broadcast_index::empty () const
{
	return entries.empty ();
}

/*
 * vote_generator_index
 */

lumex::vote_generator_index::vote_generator_index (size_t max_size_per_bucket)
{
	queue.max_size_query = [max_size_per_bucket] (auto const &) {
		return max_size_per_bucket;
	};
	queue.priority_query = [] (auto const &) {
		return size_t{ 1 };
	};
}

bool lumex::vote_generator_index::push (lumex::qualified_root const & root, lumex::block_hash const & hash, lumex::bucket_index bucket)
{
	if (auto existing = dedup.find (root); existing != dedup.end ())
	{
		if (existing->second == hash)
		{
			return false; // Duplicate
		}
		else
		{
			// Different hash for same root
			// Update dedup (old becomes stale)
			existing->second = hash;
			bool added = queue.push ({ root, hash }, { bucket });
			return added;
		}
	}
	else
	{
		// New root
		if (queue.push ({ root, hash }, { bucket }))
		{
			dedup.emplace (root, hash);
			return true;
		}
	}
	return false; // Queue full
}

auto lumex::vote_generator_index::next_batch (size_t count) -> std::deque<entry>
{
	queue.periodic_update ();

	std::deque<entry> result;
	while (result.size () < count && !queue.empty ())
	{
		auto [item, origin] = queue.next ();
		auto const & [root, hash] = item;

		// Check if the item is still valid (not stale from a later replacement)
		auto existing = dedup.find (root);
		if (existing != dedup.end () && existing->second == hash)
		{
			dedup.erase (existing);
			result.push_back (item);
		}
		// Otherwise stale — skip
	}
	return result;
}

size_t lumex::vote_generator_index::size () const
{
	return dedup.size ();
}

bool lumex::vote_generator_index::empty () const
{
	return dedup.empty ();
}
