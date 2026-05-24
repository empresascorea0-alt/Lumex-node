#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/messages/fwd.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/node/transport/transport.hpp>
#include <lumex/node/vote_with_weight_info.hpp>

namespace lumex
{
class node_observers final
{
public:
	using blocks_t = lumex::observer_set<lumex::election_status const &, std::vector<lumex::vote_with_weight_info> const &, lumex::account const &, lumex::uint128_t const &, bool, bool>;
	blocks_t blocks; // Notification upon election completion or cancellation
	lumex::observer_set<bool> wallet;
	lumex::observer_set<std::shared_ptr<lumex::vote>, std::shared_ptr<lumex::transport::channel>, lumex::vote_source, lumex::vote_code> vote;
	lumex::observer_set<lumex::block_hash const &> active_started;
	lumex::observer_set<lumex::block_hash const &> active_stopped;
	lumex::observer_set<lumex::account const &, bool> account_balance;
	lumex::observer_set<> disconnect;
	lumex::observer_set<lumex::root const &> work_cancel;
	lumex::observer_set<lumex::messages::telemetry_data const &, std::shared_ptr<lumex::transport::channel> const &> telemetry;
	lumex::observer_set<std::shared_ptr<lumex::transport::tcp_socket>> socket_connected;
	lumex::observer_set<std::shared_ptr<lumex::transport::channel>> channel_connected;

	lumex::container_info container_info () const;
};
}
