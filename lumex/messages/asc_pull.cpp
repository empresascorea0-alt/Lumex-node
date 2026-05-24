#include <lumex/lib/blocks.hpp>
#include <lumex/lib/object_stream.hpp>
#include <lumex/lib/stream.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/messages/asc_pull.hpp>
#include <lumex/messages/message_visitor.hpp>

namespace lumex::messages
{
/*
 * asc_pull_req
 */

asc_pull_req::asc_pull_req (const lumex::network_constants & constants) :
	message (constants, message_type::asc_pull_req)
{
}

asc_pull_req::asc_pull_req (bool & error, lumex::stream & stream, const message_header & header) :
	message (header)
{
	error = deserialize (stream);
}

void asc_pull_req::visit (message_visitor & visitor) const
{
	visitor.asc_pull_req (*this);
}

void asc_pull_req::serialize (lumex::stream & stream) const
{
	header.serialize (stream);
	lumex::write (stream, type);
	lumex::write_big_endian (stream, id);

	serialize_payload (stream);
}

bool asc_pull_req::deserialize (lumex::stream & stream)
{
	debug_assert (header.type == message_type::asc_pull_req);
	bool error = false;
	try
	{
		lumex::read (stream, type);
		lumex::read_big_endian (stream, id);

		deserialize_payload (stream);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void asc_pull_req::serialize_payload (lumex::stream & stream) const
{
	debug_assert (verify_consistency ());

	std::visit ([&stream] (auto && pld) { pld.serialize (stream); }, payload);
}

void asc_pull_req::deserialize_payload (lumex::stream & stream)
{
	switch (type)
	{
		case asc_pull_type::blocks:
		{
			blocks_payload pld{};
			pld.deserialize (stream);
			payload = pld;
			break;
		}
		case asc_pull_type::account_info:
		{
			account_info_payload pld{};
			pld.deserialize (stream);
			payload = pld;
			break;
		}
		case asc_pull_type::frontiers:
		{
			frontiers_payload pld{};
			pld.deserialize (stream);
			payload = pld;
			break;
		}
		default:
			throw std::runtime_error ("Unknown asc_pull_type");
	}
}

void asc_pull_req::update_header ()
{
	// TODO: Avoid serializing the payload twice
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream payload_stream (bytes);
		serialize_payload (payload_stream);
	}
	debug_assert (bytes.size () <= std::numeric_limits<uint16_t>::max ()); // Max uint16 for storing size
	debug_assert (bytes.size () >= 1);
	header.extensions = std::bitset<16> (bytes.size ());
}

std::size_t asc_pull_req::size (const message_header & header)
{
	uint16_t payload_length = lumex::narrow_cast<uint16_t> (header.extensions.to_ulong ());
	return partial_size + payload_length;
}

bool asc_pull_req::verify_consistency () const
{
	struct consistency_visitor
	{
		asc_pull_type type;

		void operator() (empty_payload) const
		{
			debug_assert (false, "missing payload");
		}
		void operator() (blocks_payload) const
		{
			debug_assert (type == asc_pull_type::blocks);
		}
		void operator() (account_info_payload) const
		{
			debug_assert (type == asc_pull_type::account_info);
		}
		void operator() (frontiers_payload) const
		{
			debug_assert (type == asc_pull_type::frontiers);
		}
	};
	std::visit (consistency_visitor{ type }, payload);
	return true; // Just for convenience of calling from asserts
}

void asc_pull_req::operator() (lumex::object_stream & obs) const
{
	message::operator() (obs); // Write common data

	obs.write ("type", type);
	obs.write ("id", id);

	std::visit ([&obs] (auto && pld) { pld (obs); }, payload); // Log payload
}

/*
 * asc_pull_req::blocks_payload
 */

void asc_pull_req::blocks_payload::serialize (lumex::stream & stream) const
{
	lumex::write (stream, start);
	lumex::write (stream, count);
	lumex::write (stream, start_type);
}

void asc_pull_req::blocks_payload::deserialize (lumex::stream & stream)
{
	lumex::read (stream, start);
	lumex::read (stream, count);
	lumex::read (stream, start_type);
}

void asc_pull_req::blocks_payload::operator() (lumex::object_stream & obs) const
{
	obs.write ("start", start);
	obs.write ("start_type", start_type);
	obs.write ("count", count);
}

/*
 * asc_pull_req::account_info_payload
 */

void asc_pull_req::account_info_payload::serialize (stream & stream) const
{
	lumex::write (stream, target);
	lumex::write (stream, target_type);
}

void asc_pull_req::account_info_payload::deserialize (stream & stream)
{
	lumex::read (stream, target);
	lumex::read (stream, target_type);
}

void asc_pull_req::account_info_payload::operator() (lumex::object_stream & obs) const
{
	obs.write ("target", target);
	obs.write ("target_type", target_type);
}

/*
 * asc_pull_req::frontiers_payload
 */

void asc_pull_req::frontiers_payload::serialize (lumex::stream & stream) const
{
	lumex::write (stream, start);
	lumex::write_big_endian (stream, count);
}

void asc_pull_req::frontiers_payload::deserialize (lumex::stream & stream)
{
	lumex::read (stream, start);
	lumex::read_big_endian (stream, count);
}

void asc_pull_req::frontiers_payload::operator() (lumex::object_stream & obs) const
{
	obs.write ("start", start);
	obs.write ("count", count);
}

/*
 * asc_pull_ack
 */

asc_pull_ack::asc_pull_ack (const lumex::network_constants & constants) :
	message (constants, message_type::asc_pull_ack)
{
}

asc_pull_ack::asc_pull_ack (bool & error, lumex::stream & stream, const message_header & header) :
	message (header)
{
	error = deserialize (stream);
}

void asc_pull_ack::visit (message_visitor & visitor) const
{
	visitor.asc_pull_ack (*this);
}

void asc_pull_ack::serialize (lumex::stream & stream) const
{
	debug_assert (header.extensions.to_ulong () > 0); // Block payload must have at least `not_a_block` terminator
	header.serialize (stream);
	lumex::write (stream, type);
	lumex::write_big_endian (stream, id);

	serialize_payload (stream);
}

bool asc_pull_ack::deserialize (lumex::stream & stream)
{
	debug_assert (header.type == message_type::asc_pull_ack);
	bool error = false;
	try
	{
		lumex::read (stream, type);
		lumex::read_big_endian (stream, id);

		deserialize_payload (stream);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void asc_pull_ack::serialize_payload (lumex::stream & stream) const
{
	debug_assert (verify_consistency ());

	std::visit ([&stream] (auto && pld) { pld.serialize (stream); }, payload);
}

void asc_pull_ack::deserialize_payload (lumex::stream & stream)
{
	switch (type)
	{
		case asc_pull_type::blocks:
		{
			blocks_payload pld{};
			pld.deserialize (stream);
			payload = pld;
			break;
		}
		case asc_pull_type::account_info:
		{
			account_info_payload pld{};
			pld.deserialize (stream);
			payload = pld;
			break;
		}
		case asc_pull_type::frontiers:
		{
			frontiers_payload pld{};
			pld.deserialize (stream);
			payload = pld;
			break;
		}
		default:
			throw std::runtime_error ("Unknown asc_pull_type");
	}
}

void asc_pull_ack::update_header ()
{
	// TODO: Avoid serializing the payload twice
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream payload_stream (bytes);
		serialize_payload (payload_stream);
	}
	debug_assert (bytes.size () <= std::numeric_limits<uint16_t>::max ()); // Max uint16 for storing size
	debug_assert (bytes.size () >= 1);
	header.extensions = std::bitset<16> (bytes.size ());
}

std::size_t asc_pull_ack::size (const message_header & header)
{
	uint16_t payload_length = lumex::narrow_cast<uint16_t> (header.extensions.to_ulong ());
	return partial_size + payload_length;
}

bool asc_pull_ack::verify_consistency () const
{
	struct consistency_visitor
	{
		asc_pull_type type;

		void operator() (empty_payload) const
		{
			debug_assert (false, "missing payload");
		}
		void operator() (blocks_payload) const
		{
			debug_assert (type == asc_pull_type::blocks);
		}
		void operator() (account_info_payload) const
		{
			debug_assert (type == asc_pull_type::account_info);
		}
		void operator() (frontiers_payload) const
		{
			debug_assert (type == asc_pull_type::frontiers);
		}
	};
	std::visit (consistency_visitor{ type }, payload);
	return true; // Just for convenience of calling from asserts
}

void asc_pull_ack::operator() (lumex::object_stream & obs) const
{
	message::operator() (obs); // Write common data

	obs.write ("type", type);
	obs.write ("id", id);

	std::visit ([&obs] (auto && pld) { pld (obs); }, payload); // Log payload
}

/*
 * asc_pull_ack::blocks_payload
 */

void asc_pull_ack::blocks_payload::serialize (lumex::stream & stream) const
{
	debug_assert (blocks.size () <= max_blocks);

	for (auto & block : blocks)
	{
		debug_assert (block != nullptr);
		lumex::serialize_block (stream, *block);
	}
	// For convenience, end with null block terminator
	lumex::write (stream, lumex::block_type::not_a_block);
}

void asc_pull_ack::blocks_payload::deserialize (lumex::stream & stream)
{
	auto current = lumex::deserialize_block (stream);
	while (current && blocks.size () < max_blocks)
	{
		blocks.push_back (current);
		current = lumex::deserialize_block (stream);
	}
}

void asc_pull_ack::blocks_payload::operator() (lumex::object_stream & obs) const
{
	obs.write_range ("blocks", blocks);
}

/*
 * asc_pull_ack::account_info_payload
 */

void asc_pull_ack::account_info_payload::serialize (lumex::stream & stream) const
{
	lumex::write (stream, account);
	lumex::write (stream, account_open);
	lumex::write (stream, account_head);
	lumex::write_big_endian (stream, account_block_count);
	lumex::write (stream, account_conf_frontier);
	lumex::write_big_endian (stream, account_conf_height);
}

void asc_pull_ack::account_info_payload::deserialize (lumex::stream & stream)
{
	lumex::read (stream, account);
	lumex::read (stream, account_open);
	lumex::read (stream, account_head);
	lumex::read_big_endian (stream, account_block_count);
	lumex::read (stream, account_conf_frontier);
	lumex::read_big_endian (stream, account_conf_height);
}

void asc_pull_ack::account_info_payload::operator() (lumex::object_stream & obs) const
{
	obs.write ("account", account);
	obs.write ("open", account_open);
	obs.write ("head", account_head);
	obs.write ("block_count", account_block_count);
	obs.write ("conf_frontier", account_conf_frontier);
	obs.write ("conf_height", account_conf_height);
}

/*
 * asc_pull_ack::frontiers_payload
 */

void asc_pull_ack::frontiers_payload::serialize_frontier (lumex::stream & stream, asc_pull_ack::frontiers_payload::frontier const & frontier)
{
	auto const & [account, hash] = frontier;
	lumex::write (stream, account);
	lumex::write (stream, hash);
}

asc_pull_ack::frontiers_payload::frontier asc_pull_ack::frontiers_payload::deserialize_frontier (lumex::stream & stream)
{
	lumex::account account;
	lumex::block_hash hash;
	lumex::read (stream, account);
	lumex::read (stream, hash);
	return { account, hash };
}

void asc_pull_ack::frontiers_payload::serialize (lumex::stream & stream) const
{
	debug_assert (frontiers.size () <= max_frontiers);

	for (auto const & frontier : frontiers)
	{
		serialize_frontier (stream, frontier);
	}
	serialize_frontier (stream, { lumex::account{ 0 }, lumex::block_hash{ 0 } });
}

void asc_pull_ack::frontiers_payload::deserialize (lumex::stream & stream)
{
	auto current = deserialize_frontier (stream);
	while ((!current.first.is_zero () && !current.second.is_zero ()) && frontiers.size () < max_frontiers)
	{
		frontiers.push_back (current);
		current = deserialize_frontier (stream);
	}
}

void asc_pull_ack::frontiers_payload::operator() (lumex::object_stream & obs) const
{
	obs.write_range ("frontiers", frontiers, [] (auto const & entry, lumex::object_stream & obs) {
		auto & [account, hash] = entry;
		obs.write ("account", account);
		obs.write ("hash", hash);
	});
}
}
