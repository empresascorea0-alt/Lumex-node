#pragma once

#include <lumex/lib/asio.hpp>
#include <lumex/lib/stats_enums.hpp>
#include <lumex/messages/fwd.hpp>

#include <memory>
#include <string_view>
#include <tuple>

namespace lumex
{
enum class deserialize_message_status
{
	success,
	insufficient_work,
	invalid_header,
	invalid_message_type,
	invalid_keepalive_message,
	invalid_publish_message,
	invalid_confirm_req_message,
	invalid_confirm_ack_message,
	invalid_node_id_handshake_message,
	invalid_telemetry_req_message,
	invalid_telemetry_ack_message,
	invalid_bulk_pull_message,
	invalid_bulk_pull_account_message,
	invalid_frontier_req_message,
	invalid_asc_pull_req_message,
	invalid_asc_pull_ack_message,
	invalid_network,
	outdated_version,
	duplicate_publish_message,
	duplicate_confirm_ack_message,
};

lumex::stat::detail to_stat_detail (deserialize_message_status);
std::string_view to_string (deserialize_message_status);

using deserialize_message_result = std::tuple<std::unique_ptr<lumex::messages::message>, lumex::deserialize_message_status>;

deserialize_message_result deserialize_message (
lumex::buffer_view buffer,
lumex::messages::message_header const & header,
lumex::network_constants const &,
lumex::network_filter * = nullptr,
lumex::block_uniquer * = nullptr,
lumex::vote_uniquer * = nullptr);
}
