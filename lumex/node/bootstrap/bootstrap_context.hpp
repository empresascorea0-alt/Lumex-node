#pragma once

#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/lib/random.hpp>
#include <lumex/lib/rate_limiting.hpp>
#include <lumex/lib/thread_pool.hpp>
#include <lumex/node/bootstrap/account_sets_index.hpp>
#include <lumex/node/bootstrap/bootstrap_config.hpp>
#include <lumex/node/bootstrap/common.hpp>
#include <lumex/node/bootstrap/database_scan_index.hpp>
#include <lumex/node/bootstrap/frontier_scan_index.hpp>
#include <lumex/node/bootstrap/peer_scoring.hpp>
#include <lumex/node/bootstrap/throttle.hpp>
#include <lumex/node/fwd.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <memory>
#include <thread>
#include <variant>

namespace mi = boost::multi_index;

namespace lumex::bootstrap
{
class priority_strategy;
class database_strategy;
class dependency_strategy;
class frontier_strategy;

struct blocks_tag_payload
{
	lumex::hash_or_account start{ 0 };
	size_t count{ 0 };
};

struct dependency_tag_payload
{
	lumex::block_hash start{ 0 };
};

struct frontier_tag_payload
{
	lumex::account start{ 0 };
};

using async_tag_payload = std::variant<blocks_tag_payload, dependency_tag_payload, frontier_tag_payload>;

struct async_tag
{
	using id_t = lumex::bootstrap::id_t;

	query_type type{ query_type::invalid };
	query_source source{ query_source::invalid };
	lumex::account account{ 0 };
	lumex::block_hash hash{ 0 };
	std::chrono::steady_clock::time_point cutoff{};
	std::chrono::steady_clock::time_point timestamp{ std::chrono::steady_clock::now () };
	id_t id{ generate_id () };

	async_tag_payload payload;
};

enum class verify_result
{
	ok,
	nothing_new,
	invalid,
};

class bootstrap_context
{
public:
	bootstrap_context (lumex::node_config const &, lumex::ledger &, lumex::ledger_notifications &, lumex::block_processor &, lumex::network &, lumex::stats &, lumex::logger &);
	~bootstrap_context ();

	void start ();
	void stop ();

	void reset ();

	/**
	 * Process bootstrap messages coming from the network
	 */
	void process (lumex::messages::asc_pull_ack const & message, std::shared_ptr<lumex::transport::channel> const &);

	/* Waits for a condition to be satisfied with incremental backoff */
	void wait (std::function<bool ()> const & predicate) const;

	/* Ensure there is enough space in block_processor for queuing new blocks */
	void wait_block_processor () const;
	/* Waits for a channel that is not full */
	std::shared_ptr<lumex::transport::channel> wait_channel ();

	bool request (lumex::account, size_t count, std::shared_ptr<lumex::transport::channel> const &, query_source);
	bool send (std::shared_ptr<lumex::transport::channel> const &, lumex::messages::asc_pull_req && message, async_tag tag);

	size_t count_tags (lumex::account const & account, query_source source) const;
	size_t count_tags (lumex::block_hash const & hash, query_source source) const;

	/* Inspects a block that has been processed by the block processor */
	void inspect (secure::transaction const &, lumex::block_status const & result, lumex::block const & block, lumex::block_source);

	// Calculates a lookback size based on the size of the ledger
	std::size_t compute_throttle_size () const;

	lumex::container_info container_info () const;

private:
	bool process (lumex::messages::asc_pull_ack::blocks_payload const & response, async_tag const & tag);
	bool process (lumex::messages::asc_pull_ack::account_info_payload const & response, async_tag const & tag);
	bool process (lumex::messages::asc_pull_ack::frontiers_payload const & response, async_tag const & tag);
	bool process (lumex::messages::empty_payload const & response, async_tag const & tag);

	verify_result verify (lumex::messages::asc_pull_ack::blocks_payload const & response, async_tag const & tag) const;

	void cleanup ();
	void run_cleanup ();

public: // Dependencies
	lumex::bootstrap_config const & config;
	lumex::network_constants const & network_constants;
	lumex::ledger & ledger;
	lumex::ledger_notifications & ledger_notifications;
	lumex::block_processor & block_processor;
	lumex::network & network;
	lumex::stats & stats;
	lumex::logger & logger;

public: // Strategies
	std::unique_ptr<lumex::bootstrap::priority_strategy> priority_strat_impl;
	lumex::bootstrap::priority_strategy & priority_strat;
	std::unique_ptr<lumex::bootstrap::database_strategy> database_strat_impl;
	lumex::bootstrap::database_strategy & database_strat;
	std::unique_ptr<lumex::bootstrap::dependency_strategy> dependency_strat_impl;
	lumex::bootstrap::dependency_strategy & dependency_strat;
	std::unique_ptr<lumex::bootstrap::frontier_strategy> frontier_strat_impl;
	lumex::bootstrap::frontier_strategy & frontier_strat;

public: // Shared state
	lumex::bootstrap::account_sets_index accounts;
	lumex::bootstrap::database_scan_index database_scan;
	lumex::bootstrap::frontier_scan_index frontiers;
	lumex::bootstrap::throttle throttle;
	lumex::bootstrap::peer_scoring scoring;

	// clang-format off
	class tag_sequenced {};
	class tag_id {};
	class tag_account {};
	class tag_hash {};

	using ordered_tags = boost::multi_index_container<async_tag,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_id>,
			mi::member<async_tag, id_t, &async_tag::id>>,
		mi::hashed_non_unique<mi::tag<tag_account>,
			mi::member<async_tag, lumex::account, &async_tag::account>>,
		mi::hashed_non_unique<mi::tag<tag_hash>,
			mi::member<async_tag, lumex::block_hash, &async_tag::hash>>
	>>;
	// clang-format on
	ordered_tags tags;

	// Rate limiter for all types of requests
	lumex::rate_limiter limiter;
	// Requests for accounts from database have much lower hitrate and could introduce strain on the network
	// A separate (lower) limiter ensures that we always reserve resources for querying accounts from priority queue
	lumex::rate_limiter database_limiter;
	// Rate limiter for frontier requests
	lumex::rate_limiter frontiers_limiter;

	bool stopped{ false };
	mutable lumex::mutex mutex;
	mutable lumex::condition_variable condition;

	std::thread cleanup_thread;

	lumex::thread_pool workers;
	lumex::random_generator_mt rng;
};
}
