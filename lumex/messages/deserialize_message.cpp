#include <lumex/lib/enum_util.hpp>
#include <lumex/lib/network_filter.hpp>
#include <lumex/lib/stream.hpp>
#include <lumex/lib/work.hpp>
#include <lumex/messages/asc_pull.hpp>
#include <lumex/messages/bulk_pull.hpp>
#include <lumex/messages/bulk_pull_account.hpp>
#include <lumex/messages/bulk_push.hpp>
#include <lumex/messages/confirm.hpp>
#include <lumex/messages/deserialize_message.hpp>
#include <lumex/messages/frontier_req.hpp>
#include <lumex/messages/keepalive.hpp>
#include <lumex/messages/node_id_handshake.hpp>
#include <lumex/messages/publish.hpp>
#include <lumex/messages/telemetry.hpp>
#include <lumex/messages/uniquers.hpp>

auto lumex::deserialize_message (
lumex::buffer_view buffer,
lumex::messages::message_header const & header,
lumex::network_constants const & network_constants,
lumex::network_filter * network_filter,
lumex::block_uniquer * block_uniquer,
lumex::vote_uniquer * vote_uniquer)
-> deserialize_message_result
{
	lumex::bufferstream stream{ buffer.data (), buffer.size () };

	switch (header.type)
	{
		case lumex::messages::message_type::keepalive:
		{
			bool error = false;
			auto message = std::make_unique<lumex::messages::keepalive> (error, stream, header);
			if (!error && at_end (stream))
			{
				return { std::move (message), deserialize_message_status::success };
			}
			return { nullptr, deserialize_message_status::invalid_keepalive_message };
		}
		break;
		case lumex::messages::message_type::publish:
		{
			lumex::uint128_t digest{ 0 };
			if (network_filter)
			{
				if (network_filter->apply (buffer.data (), buffer.size (), &digest))
				{
					return { nullptr, deserialize_message_status::duplicate_publish_message };
				}
			}

			bool error = false;
			auto message = std::make_unique<lumex::messages::publish> (error, stream, header, digest, block_uniquer);
			if (!error && at_end (stream) || !message->block)
			{
				if (!network_constants.work.validate_entry (*message->block))
				{
					return { std::move (message), deserialize_message_status::success };
				}
				else
				{
					return { nullptr, deserialize_message_status::insufficient_work };
				}
			}
			return { nullptr, deserialize_message_status::invalid_publish_message };
		}
		break;
		case lumex::messages::message_type::confirm_req:
		{
			bool error = false;
			auto message = std::make_unique<lumex::messages::confirm_req> (error, stream, header);
			if (!error && at_end (stream))
			{
				return { std::move (message), deserialize_message_status::success };
			}
			return { nullptr, deserialize_message_status::invalid_confirm_req_message };
		}
		break;
		case lumex::messages::message_type::confirm_ack:
		{
			lumex::uint128_t digest{ 0 };
			if (network_filter)
			{
				if (network_filter->apply (buffer.data (), buffer.size (), &digest))
				{
					return { nullptr, deserialize_message_status::duplicate_confirm_ack_message };
				}
			}

			bool error = false;
			auto message = std::make_unique<lumex::messages::confirm_ack> (error, stream, header, digest, vote_uniquer);
			if (!error && at_end (stream))
			{
				return { std::move (message), deserialize_message_status::success };
			}
			return { nullptr, deserialize_message_status::invalid_confirm_ack_message };
		}
		break;
		case lumex::messages::message_type::node_id_handshake:
		{
			bool error = false;
			auto message = std::make_unique<lumex::messages::node_id_handshake> (error, stream, header);
			if (!error && at_end (stream))
			{
				return { std::move (message), deserialize_message_status::success };
			}
			return { nullptr, deserialize_message_status::invalid_node_id_handshake_message };
		}
		break;
		case lumex::messages::message_type::telemetry_req:
		{
			return { std::make_unique<lumex::messages::telemetry_req> (header), deserialize_message_status::success };
		}
		break;
		case lumex::messages::message_type::telemetry_ack:
		{
			bool error = false;
			auto message = std::make_unique<lumex::messages::telemetry_ack> (error, stream, header);
			if (!error) // Intentionally not checking at_end here for forward compatibility
			{
				return { std::move (message), deserialize_message_status::success };
			}
			return { nullptr, deserialize_message_status::invalid_telemetry_ack_message };
		}
		break;
		case lumex::messages::message_type::bulk_pull:
		{
			bool error = false;
			auto message = std::make_unique<lumex::messages::bulk_pull> (error, stream, header);
			if (!error && at_end (stream))
			{
				return { std::move (message), deserialize_message_status::success };
			}
			return { nullptr, deserialize_message_status::invalid_bulk_pull_message };
		}
		break;
		case lumex::messages::message_type::bulk_pull_account:
		{
			bool error = false;
			auto message = std::make_unique<lumex::messages::bulk_pull_account> (error, stream, header);
			if (!error && at_end (stream))
			{
				return { std::move (message), deserialize_message_status::success };
			}
			return { nullptr, deserialize_message_status::invalid_bulk_pull_account_message };
		}
		break;
		case lumex::messages::message_type::bulk_push:
		{
			return { std::make_unique<lumex::messages::bulk_push> (header), deserialize_message_status::success };
		}
		break;
		case lumex::messages::message_type::frontier_req:
		{
			bool error = false;
			auto message = std::make_unique<lumex::messages::frontier_req> (error, stream, header);
			if (!error && at_end (stream))
			{
				return { std::move (message), deserialize_message_status::success };
			}
			return { nullptr, deserialize_message_status::invalid_frontier_req_message };
		}
		break;
		case lumex::messages::message_type::asc_pull_req:
		{
			bool error = false;
			auto message = std::make_unique<lumex::messages::asc_pull_req> (error, stream, header);
			if (!error)
			{
				return { std::move (message), deserialize_message_status::success };
			}
			return { nullptr, deserialize_message_status::invalid_asc_pull_req_message };
		}
		break;
		case lumex::messages::message_type::asc_pull_ack:
		{
			bool error = false;
			auto message = std::make_unique<lumex::messages::asc_pull_ack> (error, stream, header);
			if (!error)
			{
				return { std::move (message), deserialize_message_status::success };
			}
			return { nullptr, deserialize_message_status::invalid_asc_pull_ack_message };
		}
		break;
		default:
			return { nullptr, deserialize_message_status::invalid_message_type };
	}
	release_assert (false, "invalid message type");
}

lumex::stat::detail lumex::to_stat_detail (lumex::deserialize_message_status status)
{
	return lumex::enum_convert<lumex::stat::detail> (status);
}

std::string_view lumex::to_string (lumex::deserialize_message_status status)
{
	return lumex::enum_to_string (status);
}
