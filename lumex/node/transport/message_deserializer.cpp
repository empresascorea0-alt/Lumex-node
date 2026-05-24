#include <lumex/lib/enum_util.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/transport/message_deserializer.hpp>
#include <lumex/secure/network_params.hpp>

lumex::transport::message_deserializer::message_deserializer (lumex::network_constants const & network_constants_a, lumex::network_filter & network_filter_a, lumex::block_uniquer & block_uniquer_a, lumex::vote_uniquer & vote_uniquer_a,
read_query read_op) :
	read_buffer{ std::make_shared<std::vector<uint8_t>> () },
	network_constants_m{ network_constants_a },
	network_filter_m{ network_filter_a },
	block_uniquer_m{ block_uniquer_a },
	vote_uniquer_m{ vote_uniquer_a },
	read_op{ std::move (read_op) }
{
	debug_assert (this->read_op);
	read_buffer->resize (MAX_MESSAGE_SIZE);
}

void lumex::transport::message_deserializer::read (const lumex::transport::message_deserializer::callback_type && callback)
{
	debug_assert (callback);
	debug_assert (read_op);

	status = parse_status::none;

	read_op (read_buffer, HEADER_SIZE, [this_l = shared_from_this (), callback = std::move (callback)] (boost::system::error_code const & ec, std::size_t size_a) {
		if (ec)
		{
			callback (ec, nullptr);
			return;
		}
		if (size_a != HEADER_SIZE)
		{
			callback (boost::asio::error::fault, nullptr);
			return;
		}
		this_l->received_header (std::move (callback));
	});
}

void lumex::transport::message_deserializer::received_header (const lumex::transport::message_deserializer::callback_type && callback)
{
	lumex::bufferstream stream{ read_buffer->data (), HEADER_SIZE };
	auto error = false;
	lumex::messages::message_header header{ error, stream };
	if (error)
	{
		status = parse_status::invalid_header;
		callback (boost::asio::error::fault, nullptr);
		return;
	}
	if (header.network != network_constants_m.current_network)
	{
		status = parse_status::invalid_network;
		callback (boost::asio::error::fault, nullptr);
		return;
	}
	if (header.version_using < network_constants_m.protocol_version_min)
	{
		status = parse_status::outdated_version;
		callback (boost::asio::error::fault, nullptr);
		return;
	}
	if (!header.is_valid_message_type ())
	{
		status = parse_status::invalid_header;
		callback (boost::asio::error::fault, nullptr);
		return;
	}

	std::size_t payload_size = header.payload_length_bytes ();
	if (payload_size > MAX_MESSAGE_SIZE)
	{
		status = parse_status::message_size_too_big;
		callback (boost::asio::error::fault, nullptr);
		return;
	}
	debug_assert (payload_size <= read_buffer->capacity ());

	if (payload_size == 0)
	{
		// Payload size will be 0 for `bulk_push` & `telemetry_req` message type
		received_message (header, 0, std::move (callback));
	}
	else
	{
		debug_assert (read_op);
		read_op (read_buffer, payload_size, [this_l = shared_from_this (), payload_size, header, callback = std::move (callback)] (boost::system::error_code const & ec, std::size_t size_a) {
			if (ec)
			{
				callback (ec, nullptr);
				return;
			}
			if (size_a != payload_size)
			{
				callback (boost::asio::error::fault, nullptr);
				return;
			}
			this_l->received_message (header, size_a, std::move (callback));
		});
	}
}

void lumex::transport::message_deserializer::received_message (lumex::messages::message_header header, std::size_t payload_size, const lumex::transport::message_deserializer::callback_type && callback)
{
	auto message = deserialize (header, payload_size);
	if (message)
	{
		debug_assert (status == parse_status::none);
		status = parse_status::success;
		callback (boost::system::error_code{}, std::move (message));
	}
	else
	{
		debug_assert (status != parse_status::none);
		callback (boost::system::error_code{}, nullptr);
	}
}

std::unique_ptr<lumex::messages::message> lumex::transport::message_deserializer::deserialize (lumex::messages::message_header header, std::size_t payload_size)
{
	release_assert (payload_size <= MAX_MESSAGE_SIZE);
	lumex::bufferstream stream{ read_buffer->data (), payload_size };
	switch (header.type)
	{
		case lumex::messages::message_type::keepalive:
		{
			return deserialize_keepalive (stream, header);
		}
		case lumex::messages::message_type::publish:
		{
			// Early filtering to not waste time deserializing duplicates
			lumex::uint128_t digest;
			if (!network_filter_m.apply (read_buffer->data (), payload_size, &digest))
			{
				return deserialize_publish (stream, header, digest);
			}
			else
			{
				status = parse_status::duplicate_publish_message;
			}
			break;
		}
		case lumex::messages::message_type::confirm_req:
		{
			return deserialize_confirm_req (stream, header);
		}
		case lumex::messages::message_type::confirm_ack:
		{
			// Early filtering to not waste time deserializing duplicates
			lumex::uint128_t digest;
			if (!network_filter_m.apply (read_buffer->data (), payload_size, &digest))
			{
				return deserialize_confirm_ack (stream, header, digest);
			}
			else
			{
				status = parse_status::duplicate_confirm_ack_message;
			}
			break;
		}
		case lumex::messages::message_type::node_id_handshake:
		{
			return deserialize_node_id_handshake (stream, header);
		}
		case lumex::messages::message_type::telemetry_req:
		{
			return deserialize_telemetry_req (stream, header);
		}
		case lumex::messages::message_type::telemetry_ack:
		{
			return deserialize_telemetry_ack (stream, header);
		}
		case lumex::messages::message_type::bulk_pull:
		{
			return deserialize_bulk_pull (stream, header);
		}
		case lumex::messages::message_type::bulk_pull_account:
		{
			return deserialize_bulk_pull_account (stream, header);
		}
		case lumex::messages::message_type::bulk_push:
		{
			return deserialize_bulk_push (stream, header);
		}
		case lumex::messages::message_type::frontier_req:
		{
			return deserialize_frontier_req (stream, header);
		}
		case lumex::messages::message_type::asc_pull_req:
		{
			return deserialize_asc_pull_req (stream, header);
		}
		case lumex::messages::message_type::asc_pull_ack:
		{
			return deserialize_asc_pull_ack (stream, header);
		}
		default:
		{
			status = parse_status::invalid_message_type;
			break;
		}
	}
	return {};
}

std::unique_ptr<lumex::messages::keepalive> lumex::transport::message_deserializer::deserialize_keepalive (lumex::stream & stream, lumex::messages::message_header const & header)
{
	auto error = false;
	auto incoming = std::make_unique<lumex::messages::keepalive> (error, stream, header);
	if (!error && lumex::at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_keepalive_message;
	}
	return {};
}

std::unique_ptr<lumex::messages::publish> lumex::transport::message_deserializer::deserialize_publish (lumex::stream & stream, lumex::messages::message_header const & header, lumex::network_filter::digest_t const & digest_a)
{
	auto error = false;
	auto incoming = std::make_unique<lumex::messages::publish> (error, stream, header, digest_a, &block_uniquer_m);
	if (!error && lumex::at_end (stream))
	{
		release_assert (incoming->block);
		if (!network_constants_m.work.validate_entry (*incoming->block))
		{
			return incoming;
		}
		else
		{
			status = parse_status::insufficient_work;
		}
	}
	else
	{
		status = parse_status::invalid_publish_message;
	}
	return {};
}

std::unique_ptr<lumex::messages::confirm_req> lumex::transport::message_deserializer::deserialize_confirm_req (lumex::stream & stream, lumex::messages::message_header const & header)
{
	auto error = false;
	auto incoming = std::make_unique<lumex::messages::confirm_req> (error, stream, header);
	if (!error && lumex::at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_confirm_req_message;
	}
	return {};
}

std::unique_ptr<lumex::messages::confirm_ack> lumex::transport::message_deserializer::deserialize_confirm_ack (lumex::stream & stream, lumex::messages::message_header const & header, lumex::network_filter::digest_t const & digest_a)
{
	auto error = false;
	auto incoming = std::make_unique<lumex::messages::confirm_ack> (error, stream, header, digest_a, &vote_uniquer_m);
	if (!error && lumex::at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_confirm_ack_message;
	}
	return {};
}

std::unique_ptr<lumex::messages::node_id_handshake> lumex::transport::message_deserializer::deserialize_node_id_handshake (lumex::stream & stream, lumex::messages::message_header const & header)
{
	bool error = false;
	auto incoming = std::make_unique<lumex::messages::node_id_handshake> (error, stream, header);
	if (!error && lumex::at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_node_id_handshake_message;
	}
	return {};
}

std::unique_ptr<lumex::messages::telemetry_req> lumex::transport::message_deserializer::deserialize_telemetry_req (lumex::stream & stream, lumex::messages::message_header const & header)
{
	// Message does not use stream payload (header only)
	return std::make_unique<lumex::messages::telemetry_req> (header);
}

std::unique_ptr<lumex::messages::telemetry_ack> lumex::transport::message_deserializer::deserialize_telemetry_ack (lumex::stream & stream, lumex::messages::message_header const & header)
{
	bool error = false;
	auto incoming = std::make_unique<lumex::messages::telemetry_ack> (error, stream, header);
	// Intentionally not checking if at the end of stream, because these messages support backwards/forwards compatibility
	if (!error)
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_telemetry_ack_message;
	}
	return {};
}

std::unique_ptr<lumex::messages::bulk_pull> lumex::transport::message_deserializer::deserialize_bulk_pull (lumex::stream & stream, const lumex::messages::message_header & header)
{
	bool error = false;
	auto incoming = std::make_unique<lumex::messages::bulk_pull> (error, stream, header);
	if (!error && lumex::at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_bulk_pull_message;
	}
	return {};
}

std::unique_ptr<lumex::messages::bulk_pull_account> lumex::transport::message_deserializer::deserialize_bulk_pull_account (lumex::stream & stream, const lumex::messages::message_header & header)
{
	bool error = false;
	auto incoming = std::make_unique<lumex::messages::bulk_pull_account> (error, stream, header);
	if (!error && lumex::at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_bulk_pull_account_message;
	}
	return {};
}

std::unique_ptr<lumex::messages::frontier_req> lumex::transport::message_deserializer::deserialize_frontier_req (lumex::stream & stream, const lumex::messages::message_header & header)
{
	bool error = false;
	auto incoming = std::make_unique<lumex::messages::frontier_req> (error, stream, header);
	if (!error && lumex::at_end (stream))
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_frontier_req_message;
	}
	return {};
}

std::unique_ptr<lumex::messages::bulk_push> lumex::transport::message_deserializer::deserialize_bulk_push (lumex::stream & stream, const lumex::messages::message_header & header)
{
	// Message does not use stream payload (header only)
	return std::make_unique<lumex::messages::bulk_push> (header);
}

std::unique_ptr<lumex::messages::asc_pull_req> lumex::transport::message_deserializer::deserialize_asc_pull_req (lumex::stream & stream, const lumex::messages::message_header & header)
{
	bool error = false;
	auto incoming = std::make_unique<lumex::messages::asc_pull_req> (error, stream, header);
	// Intentionally not checking if at the end of stream, because these messages support backwards/forwards compatibility
	if (!error)
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_asc_pull_req_message;
	}
	return {};
}

std::unique_ptr<lumex::messages::asc_pull_ack> lumex::transport::message_deserializer::deserialize_asc_pull_ack (lumex::stream & stream, const lumex::messages::message_header & header)
{
	bool error = false;
	auto incoming = std::make_unique<lumex::messages::asc_pull_ack> (error, stream, header);
	// Intentionally not checking if at the end of stream, because these messages support backwards/forwards compatibility
	if (!error)
	{
		return incoming;
	}
	else
	{
		status = parse_status::invalid_asc_pull_ack_message;
	}
	return {};
}

/*
 *
 */

lumex::stat::detail lumex::transport::to_stat_detail (lumex::transport::parse_status status)
{
	return lumex::enum_convert<lumex::stat::detail> (status);
}

std::string_view lumex::transport::to_string (lumex::transport::parse_status status)
{
	return lumex::enum_to_string (status);
}
