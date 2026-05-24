#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/messages/telemetry.hpp>
#include <lumex/node/endpoint.hpp>
#include <lumex/node/endpoint_templ.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/node/transport/channel.hpp>
#include <lumex/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <thread>

namespace mi = boost::multi_index;

namespace lumex
{
class telemetry_config final
{
public:
	bool enable_ongoing_requests{ false }; // TODO: No longer used, remove
	bool enable_ongoing_broadcasts{ true };

public:
	explicit telemetry_config (lumex::node_flags const & flags);
};

/**
 * This class periodically broadcasts and requests telemetry from peers.
 * Those intervals are configurable via `telemetry_request_interval` & `telemetry_broadcast_interval` network constants
 * Telemetry datas are only removed after becoming stale (configurable via `telemetry_cache_cutoff` network constant), so peer data will still be available for a short period after that peer is disconnected
 *
 * Broadcasts can be disabled via `disable_providing_telemetry_metrics` node flag
 *
 */
class telemetry
{
public:
	telemetry (lumex::node_flags const &, lumex::node &, lumex::network &, lumex::node_observers &, lumex::network_params &, lumex::stats &);
	~telemetry ();

	void start ();
	void stop ();

	/**
	 * Process telemetry message from network
	 */
	void process (lumex::messages::telemetry_ack const &, std::shared_ptr<lumex::transport::channel> const &);

	/**
	 * Trigger manual telemetry request to all peers
	 */
	void trigger ();

	std::size_t size () const;

	/**
	 * Returns telemetry for selected endpoint
	 */
	std::optional<lumex::messages::telemetry_data> get_telemetry (lumex::endpoint const &) const;

	/**
	 * Returns all available telemetry
	 */
	std::unordered_map<lumex::endpoint, lumex::messages::telemetry_data> get_all_telemetries () const;

	lumex::container_info container_info () const;

private: // Dependencies
	telemetry_config const config;
	lumex::node & node;
	lumex::network & network;
	lumex::node_observers & observers;
	lumex::network_params & network_params;
	lumex::stats & stats;

private:
	struct entry
	{
		std::shared_ptr<lumex::transport::channel> channel;
		lumex::messages::telemetry_data data;
		std::chrono::steady_clock::time_point last_updated;

		lumex::endpoint endpoint () const
		{
			return channel->get_remote_endpoint ();
		}
	};

private:
	bool request_predicate () const;
	bool broadcast_predicate () const;

	void run ();
	void run_requests ();
	void run_broadcasts ();
	void cleanup ();

	void request (std::shared_ptr<lumex::transport::channel> const &);
	void broadcast (std::shared_ptr<lumex::transport::channel> const &, lumex::messages::telemetry_data const &);

	bool verify (lumex::messages::telemetry_ack const &, std::shared_ptr<lumex::transport::channel> const &) const;
	bool check_timeout (entry const &) const;

private:
	// clang-format off
	class tag_sequenced {};
	class tag_channel {};
	class tag_endpoint {};

	using ordered_telemetries = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::ordered_unique<mi::tag<tag_channel>,
			mi::member<entry,  std::shared_ptr<lumex::transport::channel>, &entry::channel>>,
		mi::hashed_non_unique<mi::tag<tag_endpoint>,
			mi::const_mem_fun<entry, lumex::endpoint, &entry::endpoint>>
	>>;
	// clang-format on

	ordered_telemetries telemetries;

	bool triggered{ false };
	std::chrono::steady_clock::time_point last_request{};
	std::chrono::steady_clock::time_point last_broadcast{};

	bool stopped{ false };
	mutable lumex::mutex mutex{ mutex_identifier (mutexes::telemetry) };
	lumex::condition_variable condition;
	std::thread thread;

private:
	static std::size_t constexpr max_size = 1024;
};
}
