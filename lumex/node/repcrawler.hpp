#pragma once

#include <lumex/lib/constants.hpp>
#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/node/transport/channel.hpp>
#include <lumex/node/transport/transport.hpp>

#include <boost/circular_buffer.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <memory>
#include <thread>
#include <unordered_set>

using namespace std::chrono_literals;

namespace mi = boost::multi_index;

namespace lumex
{
struct representative
{
	lumex::account account;
	std::shared_ptr<lumex::transport::channel> channel;
};

class rep_crawler_config final
{
public:
	explicit rep_crawler_config (lumex::network_constants const &);

	lumex::error deserialize (lumex::tomlconfig & toml);
	lumex::error serialize (lumex::tomlconfig & toml) const;

public:
	std::chrono::milliseconds query_timeout{ 1000 * 60 };
};

/**
 * Crawls the network for representatives. Queries are performed by requesting confirmation of a
 * random block and observing the corresponding vote.
 */
class rep_crawler final
{
public:
	rep_crawler (rep_crawler_config const &, lumex::node &);
	~rep_crawler ();

	void start ();
	void stop ();

	/**
	 * Called when a non-replay vote arrives that might be of interest to rep crawler.
	 * @return true, if the vote was of interest and was processed, this indicates that the rep is likely online and voting
	 */
	bool process (std::shared_ptr<lumex::vote> const &, std::shared_ptr<lumex::transport::channel> const &);

	/** Attempt to determine if the peer manages one or more representative accounts */
	void query (std::deque<std::shared_ptr<lumex::transport::channel>> const & target_channels);

	/** Attempt to determine if the peer manages one or more representative accounts */
	void query (std::shared_ptr<lumex::transport::channel> const & target_channel);

	/** Query if a peer manages a principle representative */
	bool is_pr (std::shared_ptr<lumex::transport::channel> const &) const;

	/** Get total available weight from representatives */
	lumex::uint128_t total_weight () const;

	/** Request a list of the top \p count known representatives in descending order of weight, with at least \p weight_a voting weight, and optionally with a minimum version \p minimum_protocol_version */
	std::vector<representative> representatives (std::size_t count = std::numeric_limits<std::size_t>::max (), lumex::uint128_t minimum_weight = 0, std::optional<decltype (lumex::network_constants::protocol_version)> const & minimum_protocol_version = {}) const;

	/** Request a list of the top \p count known principal representatives in descending order of weight, optionally with a minimum version \p minimum_protocol_version */
	std::vector<representative> principal_representatives (std::size_t count = std::numeric_limits<std::size_t>::max (), std::optional<decltype (lumex::network_constants::protocol_version)> const & minimum_protocol_version = {}) const;

	/** Total number of representatives */
	std::size_t representative_count () const;

	lumex::container_info container_info () const;

private: // Dependencies
	rep_crawler_config const & config;
	lumex::node & node;
	lumex::stats & stats;
	lumex::logger & logger;
	lumex::network_constants & network_constants;
	lumex::active_elections & active;

private:
	void run ();
	void cleanup ();
	void validate_and_process (lumex::unique_lock<lumex::mutex> &);
	bool query_predicate (bool sufficient_weight) const;
	std::chrono::milliseconds query_interval (bool sufficient_weight) const;

	using hash_root_t = std::pair<lumex::block_hash, lumex::root>;

	/** Returns a list of endpoints to crawl. The total weight is passed in to avoid computing it twice. */

	std::deque<std::shared_ptr<lumex::transport::channel>> prepare_crawl_targets (bool sufficient_weight) const;
	hash_root_t prepare_query_target () const;
	bool track_rep_request (hash_root_t hash_root, std::shared_ptr<lumex::transport::channel> const & channel);

private:
	/**
	 * A representative picked up during repcrawl.
	 */
	struct rep_entry
	{
		rep_entry (lumex::account account_a, std::shared_ptr<lumex::transport::channel> const & channel_a) :
			account{ account_a },
			channel{ channel_a }
		{
			debug_assert (channel != nullptr);
		}

		lumex::account const account;
		std::shared_ptr<lumex::transport::channel> channel;

		std::chrono::steady_clock::time_point last_request{};
		std::chrono::steady_clock::time_point last_response{ std::chrono::steady_clock::now () };

		lumex::account get_account () const
		{
			return account;
		}
	};

	struct query_entry
	{
		lumex::block_hash hash;
		std::shared_ptr<lumex::transport::channel> channel;
		std::chrono::steady_clock::time_point time{ std::chrono::steady_clock::now () };
		unsigned int replies{ 0 }; // number of replies to the query

		std::pair<std::shared_ptr<lumex::transport::channel>, lumex::block_hash> unique_key () const
		{
			return std::make_pair (channel, hash);
		}
	};

	// clang-format off
	class tag_hash {};
	class tag_account {};
	class tag_channel {};
	class tag_sequenced {};
	class tag_unique {};

	using ordered_reps = boost::multi_index_container<rep_entry,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_account>,
			mi::const_mem_fun<rep_entry, lumex::account, &rep_entry::get_account>>,
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_non_unique<mi::tag<tag_channel>,
			mi::member<rep_entry, std::shared_ptr<lumex::transport::channel>, &rep_entry::channel>>
	>>;

	using ordered_queries = boost::multi_index_container<query_entry,
	mi::indexed_by<
		mi::hashed_non_unique<mi::tag<tag_channel>,
			mi::member<query_entry, std::shared_ptr<lumex::transport::channel>, &query_entry::channel>>,
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_non_unique<mi::tag<tag_hash>,
			mi::member<query_entry, lumex::block_hash, &query_entry::hash>>,
		mi::hashed_unique<mi::tag<tag_unique>,
			mi::const_mem_fun<query_entry, std::pair<std::shared_ptr<lumex::transport::channel>, lumex::block_hash>, &query_entry::unique_key>>
	>>;
	// clang-format on

	ordered_reps reps;
	ordered_queries queries;

private:
	static size_t constexpr max_responses{ 1024 * 4 };

	using response_t = std::pair<std::shared_ptr<lumex::transport::channel>, std::shared_ptr<lumex::vote>>;
	boost::circular_buffer<response_t> responses{ max_responses };

	// Freshly established connections that should be queried asap
	std::deque<std::shared_ptr<lumex::transport::channel>> prioritized;

	std::chrono::steady_clock::time_point last_query{};

	std::atomic<bool> stopped{ false };
	lumex::condition_variable condition;
	mutable lumex::mutex mutex;
	std::thread thread;

public: // Testing
	void force_add_rep (lumex::account const & account, std::shared_ptr<lumex::transport::channel> const & channel);
	void force_process (std::shared_ptr<lumex::vote> const & vote, std::shared_ptr<lumex::transport::channel> const & channel);
	void force_query (lumex::block_hash const & hash, std::shared_ptr<lumex::transport::channel> const & channel);
};
}
