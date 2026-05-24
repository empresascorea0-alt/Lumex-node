#pragma once

#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/node/endpoint.hpp>
#include <lumex/node/endpoint_templ.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

namespace mi = boost::multi_index;

namespace lumex
{
class peer_exclusion final
{
	class item final
	{
	public:
		std::chrono::steady_clock::time_point exclude_until;
		decltype (std::declval<lumex::tcp_endpoint> ().address ()) address;
		uint64_t score;
	};

public:
	explicit peer_exclusion (std::size_t max_size = 5000);

private:
	std::size_t const max_size;

	// clang-format off
	class tag_endpoint {};
	class tag_exclusion {};

	using ordered_endpoints = boost::multi_index_container<peer_exclusion::item,
	mi::indexed_by<
		mi::ordered_non_unique<mi::tag<tag_exclusion>,
			mi::member<peer_exclusion::item, std::chrono::steady_clock::time_point, &peer_exclusion::item::exclude_until>>,
		mi::hashed_unique<mi::tag<tag_endpoint>,
			mi::member<peer_exclusion::item, decltype(peer_exclusion::item::address), &peer_exclusion::item::address>>>>;
	// clang-format on

	ordered_endpoints peers;

	mutable lumex::mutex mutex;

public:
	constexpr static uint64_t score_limit = 2;
	constexpr static std::chrono::hours exclude_time_hours = std::chrono::hours (1);
	constexpr static std::chrono::hours exclude_remove_hours = std::chrono::hours (24);

	uint64_t add (lumex::tcp_endpoint const &);
	uint64_t score (lumex::tcp_endpoint const &) const;
	std::chrono::steady_clock::time_point until (lumex::tcp_endpoint const &) const;
	bool check (lumex::tcp_endpoint const &) const;
	bool check (boost::asio::ip::address const &) const;
	void remove (lumex::tcp_endpoint const &);
	std::size_t size () const;

	lumex::container_info container_info () const;
};
}
