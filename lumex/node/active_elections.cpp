#include <lumex/lib/block_type.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/enum_util.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/threading.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/node/cementing_set.hpp>
#include <lumex/node/confirmation_solicitor.hpp>
#include <lumex/node/election.hpp>
#include <lumex/node/fork_cache.hpp>
#include <lumex/node/ledger_notifications.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/node_observers.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/online_reps.hpp>
#include <lumex/node/repcrawler.hpp>
#include <lumex/node/scheduler/component.hpp>
#include <lumex/node/scheduler/priority.hpp>
#include <lumex/node/vote_cache.hpp>
#include <lumex/node/vote_processor.hpp>
#include <lumex/node/vote_router.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_set_any.hpp>
#include <lumex/secure/ledger_set_cemented.hpp>

#include <ranges>

using namespace std::chrono;

lumex::active_elections::active_elections (lumex::node & node_a, lumex::ledger_notifications & ledger_notifications_a, lumex::cementing_set & cementing_set_a) :
	config{ *node_a.config.active_elections },
	node{ node_a },
	ledger_notifications{ ledger_notifications_a },
	cementing_set{ cementing_set_a },
	recently_confirmed{ config.confirmation_cache },
	recently_cemented{ config.confirmation_cache },
	workers{ 1, lumex::thread_role::name::aec_notifications }
{
	// Cementing blocks might implicitly confirm dependent elections
	cementing_set.batch_cemented.add ([this] (auto const & cemented) {
		std::deque<block_cemented_result> results;
		{
			// Process all cemented blocks while holding the lock to avoid races where an election for a block that is already cemented is inserted
			lumex::lock_guard<lumex::mutex> guard{ mutex };
			for (auto const & [block, confirmation_root, source_election] : cemented)
			{
				auto result = block_cemented (block, confirmation_root, source_election);
				results.push_back (result);
			}
		}

		if (workers.queued_tasks () >= lumex::queue_warning_threshold () && warning_interval.elapse (15s))
		{
			node.logger.warn (lumex::log::type::active_elections, "Notification queue has {} tasks", workers.queued_tasks ());
		}

		// Notify observers about cemented blocks on a background thread
		workers.post ([this, results = std::move (results)] () {
			auto transaction = node.ledger.tx_begin_read ();
			for (auto const & [election, status, votes] : results)
			{
				transaction.refresh_if_needed ();

				// Dependent elections are cancelled when their block is cemented
				if (election)
				{
					bool cancelled = election->cancel ();
					if (cancelled)
					{
						node.stats.inc (lumex::stat::type::active_elections, lumex::stat::detail::cancel_dependent);
					}
				}

				notify_observers (transaction, status, votes);
			}
		});
	});

	// Notify elections about alternative (forked) blocks
	ledger_notifications.blocks_processed.add ([this] (auto const & batch) {
		for (auto const & [result, context] : batch)
		{
			if (result == lumex::block_status::fork)
			{
				publish (context.block);
			}
		}
	});

	// Stop all rolled back active transactions except initial
	ledger_notifications.blocks_rolled_back.add ([this] (auto const & blocks, auto const & rollback_root) {
		for (auto const & block : blocks)
		{
			if (block->qualified_root () != rollback_root)
			{
				erase (block->qualified_root ());
			}
		}
	});
}

lumex::active_elections::~active_elections ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void lumex::active_elections::start ()
{
	workers.start ();

	if (node.flags.disable_request_loop)
	{
		return;
	}

	debug_assert (!thread.joinable ());

	thread = std::thread ([this] () {
		lumex::thread_role::set (lumex::thread_role::name::aec_loop);
		run ();
	});

	checkup_thread = std::thread ([this] () {
		lumex::thread_role::set (lumex::thread_role::name::aec_checkup);
		run_checkup ();
	});
}

void lumex::active_elections::stop ()
{
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	join_or_pass (thread);
	join_or_pass (checkup_thread);
	workers.stop ();

	clear ();
}

auto lumex::active_elections::insert (std::shared_ptr<lumex::block> const & block, lumex::election_behavior behavior, lumex::bucket_index bucket, lumex::priority_timestamp priority, erased_callback_t erased_callback) -> insert_result
{
	release_assert (block);
	release_assert (block->has_sideband ());

	lumex::unique_lock<lumex::mutex> lock{ mutex };

	insert_result result{ nullptr, false };

	if (stopped)
	{
		return result;
	}

	auto const root = block->qualified_root ();
	auto const hash = block->hash ();

	if (!index.exists (root))
	{
		if (!recently_confirmed.contains (root) && !recently_cemented.contains (root))
		{
			result.inserted = true;

			// Passing this callback into the election is important
			// We need to observe and update the online voting weight *before* election quorum is checked
			auto observe_rep_action = [&node = node] (auto const & rep) {
				node.online_reps.observe (rep);
			};

			// On any election state update, schedule a call to tick it immediately
			auto update_action = [this] (auto const & root) {
				trigger (root);
			};

			result.election = std::make_shared<lumex::election> (node, block, behavior, bucket, nullptr, observe_rep_action, update_action);

			// Store erased callback if provided
			if (erased_callback)
			{
				erased_callbacks[root] = std::move (erased_callback);
			}

			// Insert the election into index
			index.insert (result.election, behavior, bucket, priority);

			node.vote_router.connect (hash, result.election);

			node.stats.inc (lumex::stat::type::active_elections, lumex::stat::detail::started);
			node.stats.inc (lumex::stat::type::active_elections_started, to_stat_detail (behavior));

			node.logger.trace (lumex::log::type::active_elections, lumex::log::detail::active_started,
			lumex::log::arg{ "behavior", behavior },
			lumex::log::arg{ "election", result.election });

			node.logger.debug (lumex::log::type::active_elections, "Started new election for root: {} with blocks: {} (behavior: {})",
			root,
			fmt::join (result.election->blocks_hashes (), ", "), // TODO: Lazy eval
			to_string (behavior));

			// Notify observers that a new election started (while still holding the lock to avoid races)
			election_started.notify (result.election, bucket, priority);
		}
		else
		{
			// Result is not set
		}
	}
	else
	{
		result.election = index.election (root);

		// The existing election should already contain this block
		debug_assert (result.election->contains (hash));

		// Upgrade to priority election to enable immediate vote broadcasting.
		auto previous_behavior = result.election->behavior ();
		if (behavior == lumex::election_behavior::priority && previous_behavior != lumex::election_behavior::priority)
		{
			bool transitioned = result.election->transition_priority ();
			if (transitioned)
			{
				index.update (result.election, behavior);
				node.stats.inc (lumex::stat::type::active_elections, lumex::stat::detail::transition_priority);

				// Notify observers that this election is now priority (same as election_started)
				election_started.notify (result.election, bucket, priority);
			}
			else
			{
				node.stats.inc (lumex::stat::type::active_elections, lumex::stat::detail::transition_priority_failed);
			}
		}
	}

	lock.unlock ();

	if (result.inserted)
	{
		release_assert (result.election);

		auto should_activate_immediately = [&] () {
			// Skip passive phase for blocks without cached votes to avoid bootstrap delays
			if (!node.vote_cache.contains (hash))
			{
				return true;
			}
			return false;
		};

		// Transition to active (broadcasting votes, sending confirm reqs) state if needed
		bool activate_immediately = should_activate_immediately ();
		if (activate_immediately)
		{
			node.stats.inc (lumex::stat::type::active_elections, lumex::stat::detail::activate_immediately);
			result.election->transition_active ();
		}

		// Notifications
		node.observers.active_started.notify (hash);
		vacancy_updated.notify ();

		// Let the election know about already observed votes
		node.vote_cache_processor.trigger (hash);

		// Let the election know about already observed forks
		auto forks = node.fork_cache.get (root);
		node.stats.add (lumex::stat::type::active_elections, lumex::stat::detail::forks_cached, forks.size ());
		for (auto const & fork : forks)
		{
			publish (fork);
		}
	}

	// Votes are generated for inserted or ongoing elections
	if (result.election)
	{
		result.election->broadcast_vote ();
	}

	return result;
}

bool lumex::active_elections::publish (std::shared_ptr<lumex::block> const & block)
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };

	if (auto election = index.election (block->qualified_root ()))
	{
		lock.unlock ();

		bool result = election->publish (block); // false => new block was added
		if (!result)
		{
			node.vote_router.connect (block->hash (), election);
			node.vote_cache_processor.trigger (block->hash ());

			node.stats.inc (lumex::stat::type::active_elections, lumex::stat::detail::fork);

			node.logger.debug (lumex::log::type::active_elections, "Block was added to an existing election: {} with root: {} (behavior: {}, state: {}, voters: {}, blocks: {}, duration: {}ms)",
			block->hash (),
			election->qualified_root,
			to_string (election->behavior ()),
			to_string (election->state ()),
			election->voter_count (),
			election->block_count (),
			election->duration ().count ());

			return false; // Added
		}
	}

	return true; // Not added
}

void lumex::active_elections::erase_election (lumex::unique_lock<lumex::mutex> & lock, std::shared_ptr<lumex::election> election)
{
	debug_assert (!mutex.try_lock ());
	debug_assert (lock.owns_lock ());
	debug_assert (!election->confirmed () || recently_confirmed.contains (election->qualified_root));

	auto blocks_l = election->blocks ();
	node.vote_router.disconnect (*election);

	// Erase from index
	bool erased = index.erase (election);
	release_assert (erased);

	// Get and remove the erased callback
	auto callback_it = erased_callbacks.find (election->qualified_root);
	erased_callback_t erased_callback;
	if (callback_it != erased_callbacks.end ())
	{
		erased_callback = std::move (callback_it->second);
		erased_callbacks.erase (callback_it);
	}

	node.stats.inc (lumex::stat::type::active_elections, lumex::stat::detail::stopped);
	node.stats.inc (lumex::stat::type::active_elections, election->confirmed () ? lumex::stat::detail::confirmed : lumex::stat::detail::unconfirmed);
	node.stats.inc (lumex::stat::type::active_elections_stopped, to_stat_detail (election->state ()));
	node.stats.inc (to_stat_type (election->state ()), to_stat_detail (election->behavior ()));

	node.logger.trace (lumex::log::type::active_elections, lumex::log::detail::active_stopped, lumex::log::arg{ "election", election });

	node.logger.debug (lumex::log::type::active_elections, "Erased election for root: {} with blocks: {} (behavior: {}, state: {}, voters: {}, blocks: {}, duration: {}ms)",
	election->qualified_root,
	fmt::join (election->blocks_hashes (), ", "), // TODO: Lazy eval
	to_string (election->behavior ()),
	to_string (election->state ()),
	election->voter_count (),
	election->block_count (),
	election->duration ().count ());

	lock.unlock ();

	// Track election duration
	node.stats.sample (lumex::stat::sample::active_election_duration, election->duration ().count (), { 0, 1000 * 60 * 10 /* 0-10 minutes range */ });

	// Notify observers without holding the lock
	if (erased_callback)
	{
		erased_callback (election);
	}

	// Notify observers that the election was erased
	election_erased.notify (election);

	vacancy_updated.notify ();

	for (auto const & [hash, block] : blocks_l)
	{
		// Notify observers about dropped elections & blocks lost confirmed elections
		if (!election->confirmed () || hash != election->winner ()->hash ())
		{
			node.observers.active_stopped.notify (hash);
		}

		if (!election->confirmed ())
		{
			// Clear from publish filter
			node.network.filter.clear (block);
		}
	}
}

bool lumex::active_elections::erase (lumex::qualified_root const & root)
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };

	if (auto election = index.election (root))
	{
		release_assert (election->qualified_root == root);
		erase_election (lock, election);
		return true;
	}
	else
	{
		return false;
	}
}

bool lumex::active_elections::erase (lumex::block const & block)
{
	return erase (block.qualified_root ());
}

auto lumex::active_elections::block_cemented (std::shared_ptr<lumex::block> const & block, lumex::block_hash const & confirmation_root, std::shared_ptr<lumex::election> const & source_election) -> block_cemented_result
{
	debug_assert (!mutex.try_lock ());
	debug_assert (node.block_confirmed (block->hash ()));

	// Dependent elections are implicitly confirmed when their block is cemented
	auto election = election_impl (block->qualified_root ());

	lumex::election_status status;
	std::vector<lumex::vote_with_weight_info> votes;
	status.winner = block;

	// Check if the currently cemented block was part of an election that triggered the confirmation
	if (source_election && source_election->qualified_root == block->qualified_root ())
	{
		status = source_election->get_status ();
		debug_assert (status.winner->hash () == block->hash ());
		votes = source_election->votes_with_weight ();
		status.type = lumex::election_status_type::active_confirmed_quorum;
	}
	else if (election)
	{
		status.type = lumex::election_status_type::active_confirmation_height;
	}
	else
	{
		status.type = lumex::election_status_type::inactive_confirmation_height;
	}

	recently_cemented.put (status);

	node.stats.inc (lumex::stat::type::active_elections, lumex::stat::detail::cemented);
	node.stats.inc (lumex::stat::type::active_elections_cemented, to_stat_detail (status.type));

	node.logger.debug (lumex::log::type::active_elections, "Cemented root: {} with block: {} (status: {})",
	block->qualified_root (),
	block->hash (),
	to_string (status.type));

	node.logger.trace (lumex::log::type::active_elections, lumex::log::detail::active_cemented,
	lumex::log::arg{ "block", block },
	lumex::log::arg{ "confirmation_root", confirmation_root },
	lumex::log::arg{ "source_election", source_election });

	return { election, status, votes };
}

void lumex::active_elections::notify_observers (lumex::secure::transaction const & transaction, lumex::election_status const & status, std::vector<lumex::vote_with_weight_info> const & votes) const
{
	// Get block from ledger to ensure sideband is set (forked blocks may not have sideband)
	auto const block = node.ledger.any.block_get (transaction, status.winner->hash ());
	release_assert (block != nullptr); // Block must exist in the ledger since it was cemented
	auto const account = block->account ();

	switch (status.type)
	{
		case lumex::election_status_type::active_confirmed_quorum:
			node.stats.inc (lumex::stat::type::confirmation_observer, lumex::stat::detail::active_quorum, lumex::stat::dir::out);
			break;
		case lumex::election_status_type::active_confirmation_height:
			node.stats.inc (lumex::stat::type::confirmation_observer, lumex::stat::detail::active_conf_height, lumex::stat::dir::out);
			break;
		case lumex::election_status_type::inactive_confirmation_height:
			node.stats.inc (lumex::stat::type::confirmation_observer, lumex::stat::detail::inactive_conf_height, lumex::stat::dir::out);
			break;
		default:
			break;
	}

	if (!node.observers.blocks.empty ())
	{
		auto amount = node.ledger.any.block_amount (transaction, block).value_or (0).number ();
		auto is_state_send = block->type () == block_type::state && block->is_send ();
		auto is_state_epoch = block->type () == block_type::state && block->is_epoch ();
		node.observers.blocks.notify (status, votes, account, amount, is_state_send, is_state_epoch);
	}

	node.observers.account_balance.notify (account, false);

	if (block->is_send ())
	{
		node.observers.account_balance.notify (block->destination (), true);
	}
}

bool lumex::active_elections::trigger (lumex::qualified_root const & root)
{
	bool triggered = false;
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		if (auto election = index.election (root))
		{
			triggered = index.trigger (election);
		}
	}
	if (triggered)
	{
		condition.notify_all ();
	}
	return triggered;
}

void lumex::active_elections::tick_elections (lumex::unique_lock<lumex::mutex> & lock)
{
	debug_assert (lock.owns_lock ());

	auto const now = std::chrono::steady_clock::now ();
	auto const cutoff = now - node.network_params.network.aec_loop_interval;
	auto const election_list = index.list (cutoff, now);
	debug_assert (!election_list.empty ()); // Shouldn't be called if there are no elections to process

	lock.unlock ();

	lumex::confirmation_solicitor solicitor (node.network, node.config);
	solicitor.prepare (node.rep_crawler.principal_representatives (std::numeric_limits<std::size_t>::max ()));

	for (auto const & election : election_list)
	{
		bool tick_result = election->tick (solicitor);
		if (tick_result)
		{
			erase (election->qualified_root);
		}
	}

	solicitor.flush ();
}

bool lumex::active_elections::predicate () const
{
	debug_assert (!mutex.try_lock ());

	auto cutoff = std::chrono::steady_clock::now () - node.network_params.network.aec_loop_interval;
	return index.any (cutoff);
}

void lumex::active_elections::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		if (predicate ())
		{
			node.stats.inc (lumex::stat::type::active, lumex::stat::detail::loop);

			tick_elections (lock);
			debug_assert (!lock.owns_lock ());
			lock.lock ();
		}
		else
		{
			condition.wait_for (lock, node.network_params.network.aec_loop_interval / 2, [this] {
				return stopped || predicate ();
			});
		}
	}
}

void lumex::active_elections::checkup_elections (lumex::unique_lock<lumex::mutex> & lock)
{
	auto all_elections = index.list ();

	lock.unlock ();

	auto transaction = node.ledger.tx_begin_read ();

	auto should_cancel = [&] (std::shared_ptr<lumex::election> const & election) {
		auto const target = node.ledger.any.block_successor (transaction, election->qualified_root);
		if (target)
		{
			// Cancel if the election's block is already cemented
			return node.ledger.cemented.block_exists (transaction, *target);
		}
		else
		{
			// No successor means the block is not in the ledger, rather unexpected
			return true; // Cancel the election
		}
	};

	auto const now = std::chrono::steady_clock::now ();
	auto const min_duration = node.network_params.network.aec_loop_interval * 3;

	std::deque<std::shared_ptr<lumex::election>> stale_elections;

	for (auto const & election : all_elections)
	{
		// Only cancel elections if they have been running for a minimum duration of time
		// Usually the normal cemented callback will handle the cleanup
		if ((now - election->get_state_start ()) > min_duration && should_cancel (election))
		{
			bool cancelled = election->cancel ();
			if (cancelled)
			{
				node.stats.inc (lumex::stat::type::active_elections, lumex::stat::detail::cancel_checkup);
				node.logger.debug (lumex::log::type::active_elections, "Checkup cancelled election for root: {} with blocks: {} (behavior: {}, state: {}, voters: {}, blocks: {}, duration: {}ms)",
				election->qualified_root,
				fmt::join (election->blocks_hashes (), ", "), // TODO: Lazy eval
				to_string (election->behavior ()),
				to_string (election->state ()),
				election->voter_count (),
				election->block_count (),
				election->duration ().count ());
			}
		}
		else if (election->duration () > config.stale_threshold)
		{
			stale_elections.push_back (election);
		}
	}

	node.logger.debug (lumex::log::type::active_elections, "Checkup found {} stale elections", stale_elections.size ());

	// Notify about stale elections at most once per half stale threshold, avoid too frequent notifications
	if (stale_interval.elapse (config.stale_threshold / 2))
	{
		node.stats.add (lumex::stat::type::active_elections, lumex::stat::detail::stale, stale_elections.size ());

		for (auto const & election : stale_elections)
		{
			node.logger.debug (lumex::log::type::active_elections, "Stale election for account: {} with root: {} blocks: {} (behavior: {}, state: {}, voters: {}, blocks: {}, duration: {}ms)",
			election->account,
			election->qualified_root,
			fmt::join (election->blocks_hashes (), ", "), // TODO: Lazy eval
			to_string (election->behavior ()),
			to_string (election->state ()),
			election->voter_count (),
			election->block_count (),
			election->duration ().count ());

			election_stale.notify (election);
		}
	}
}

void lumex::active_elections::run_checkup ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		// Ignore predicate in condition, this loop should be woken up periodically
		condition.wait_for (lock, config.checkup_interval, [this] {
			return stopped;
		});

		if (stopped)
		{
			return;
		}

		if (!index.empty ())
		{
			node.stats.inc (lumex::stat::type::active, lumex::stat::detail::loop_checkup);

			checkup_elections (lock);
			debug_assert (!lock.owns_lock ());
			lock.lock ();
		}
	}
}

int64_t lumex::active_elections::limit (lumex::election_behavior behavior) const
{
	switch (behavior)
	{
		case lumex::election_behavior::manual:
		{
			return std::numeric_limits<int64_t>::max ();
		}
		case lumex::election_behavior::priority:
		{
			return static_cast<int64_t> (config.size);
		}
		case lumex::election_behavior::hinted:
		{
			const uint64_t limit = config.hinted_limit_percentage * config.size / 100;
			return static_cast<int64_t> (limit);
		}
		case lumex::election_behavior::optimistic:
		{
			const uint64_t limit = config.optimistic_limit_percentage * config.size / 100;
			return static_cast<int64_t> (limit);
		}
	}

	debug_assert (false, "unknown election behavior");
	return 0;
}

int64_t lumex::active_elections::vacancy (lumex::election_behavior behavior) const
{
	auto election_vacancy = [this] (lumex::election_behavior behavior) -> int64_t {
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		switch (behavior)
		{
			case lumex::election_behavior::manual:
				return std::numeric_limits<int64_t>::max ();
			case lumex::election_behavior::priority:
				return limit (lumex::election_behavior::priority) - static_cast<int64_t> (index.size ());
			case lumex::election_behavior::hinted:
			case lumex::election_behavior::optimistic:
				return limit (behavior) - static_cast<int64_t> (index.size (behavior));
		}
		debug_assert (false); // Unknown enum
		return 0;
	};

	auto election_winners_vacancy = [this] () -> int64_t {
		return static_cast<int64_t> (config.max_election_winners) - static_cast<int64_t> (cementing_set.size ());
	};

	return std::min (election_vacancy (behavior), election_winners_vacancy ());
}

std::vector<std::shared_ptr<lumex::election>> lumex::active_elections::list_active (std::size_t max_count)
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return list_active_impl (max_count);
}

std::vector<std::shared_ptr<lumex::election>> lumex::active_elections::list_active_impl (std::size_t max_count) const
{
	std::vector<std::shared_ptr<lumex::election>> result_l;
	auto entries = index.list ();
	result_l.reserve (std::min (max_count, entries.size ()));
	for (auto const & entry : entries)
	{
		if (result_l.size () >= max_count)
		{
			break;
		}
		result_l.push_back (entry);
	}
	return result_l;
}

bool lumex::active_elections::active (lumex::qualified_root const & root_a) const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	return index.exists (root_a);
}

bool lumex::active_elections::active (lumex::block const & block_a) const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return index.exists (block_a.qualified_root ());
}

std::shared_ptr<lumex::election> lumex::active_elections::election (lumex::qualified_root const & root) const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	return election_impl (root);
}

std::shared_ptr<lumex::election> lumex::active_elections::election_impl (lumex::qualified_root const & root) const
{
	debug_assert (!mutex.try_lock ());
	return index.election (root);
}

bool lumex::active_elections::empty () const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	return index.size () == 0;
}

std::size_t lumex::active_elections::size () const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	return index.size ();
}

std::size_t lumex::active_elections::size (lumex::election_behavior behavior) const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	return index.size (behavior);
}

std::size_t lumex::active_elections::size (lumex::election_behavior behavior, lumex::bucket_index bucket) const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	return index.size (behavior, bucket);
}

std::size_t lumex::active_elections::stale_count () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	std::size_t count = 0;
	for (auto const & election : index.list ())
	{
		if (election->duration () > config.stale_threshold)
		{
			++count;
		}
	}
	return count;
}

void lumex::active_elections::clear ()
{
	// TODO: Call erased_callback for each election
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		index.clear ();
		erased_callbacks.clear ();
	}
	vacancy_updated.notify ();
}

lumex::container_info lumex::active_elections::container_info () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	lumex::container_info info;
	info.put ("roots", index.size ());
	info.put ("normal", index.size (lumex::election_behavior::priority));
	info.put ("hinted", index.size (lumex::election_behavior::hinted));
	info.put ("optimistic", index.size (lumex::election_behavior::optimistic));

	info.add ("recently_confirmed", recently_confirmed.container_info ());
	info.add ("recently_cemented", recently_cemented.container_info ());
	info.add ("workers", workers.container_info ());

	return info;
}

/*
 * active_elections_config
 */

lumex::active_elections_config::active_elections_config (const lumex::network_constants & network_constants)
{
}

lumex::error lumex::active_elections_config::serialize (lumex::tomlconfig & toml) const
{
	toml.put ("size", size, "Number of active elections. Elections beyond this limit have limited survival time.\nWarning: modifying this value may result in a lower confirmation rate. \ntype:uint64,[250..]");
	toml.put ("hinted_limit_percentage", hinted_limit_percentage, "Limit of hinted elections as percentage of `active_elections_size` \ntype:uint64");
	toml.put ("optimistic_limit_percentage", optimistic_limit_percentage, "Limit of optimistic elections as percentage of `active_elections_size`. \ntype:uint64");
	toml.put ("confirmation_cache", confirmation_cache, "Maximum number of confirmed elections kept in cache to prevent restarting an election. \ntype:uint64");
	toml.put ("max_election_winners", max_election_winners, "Maximum size of election winner details set. \ntype:uint64");
	toml.put ("stale_threshold", stale_threshold.count (), "Time after which additional bootstrap attempts are made to find missing blocks for an election. \ntype:seconds");
	return toml.get_error ();
}

lumex::error lumex::active_elections_config::deserialize (lumex::tomlconfig & toml)
{
	toml.get ("size", size);
	toml.get ("hinted_limit_percentage", hinted_limit_percentage);
	toml.get ("optimistic_limit_percentage", optimistic_limit_percentage);
	toml.get ("confirmation_cache", confirmation_cache);
	toml.get ("max_election_winners", max_election_winners);
	toml.get_duration ("stale_threshold", stale_threshold);

	return toml.get_error ();
}

/*
 *
 */

lumex::stat::type lumex::to_stat_type (lumex::election_state state)
{
	switch (state)
	{
		case election_state::passive:
		case election_state::active:
			return lumex::stat::type::active_elections_dropped;
			break;
		case election_state::confirmed:
		case election_state::expired_confirmed:
			return lumex::stat::type::active_elections_confirmed;
			break;
		case election_state::expired_unconfirmed:
			return lumex::stat::type::active_elections_timeout;
			break;
		case election_state::cancelled:
			return lumex::stat::type::active_elections_cancelled;
			break;
	}
	debug_assert (false);
	return {};
}

std::string_view lumex::to_string (lumex::election_status_type type)
{
	return lumex::enum_to_string (type);
}

lumex::stat::detail lumex::to_stat_detail (lumex::election_status_type type)
{
	return lumex::enum_convert<lumex::stat::detail> (type);
}
