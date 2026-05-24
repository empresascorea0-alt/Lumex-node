#include <lumex/lib/block_sideband.hpp>
#include <lumex/lib/block_type.hpp>
#include <lumex/lib/object_stream.hpp>
#include <lumex/lib/stream.hpp>

#include <bitset>

/*
 * block_details
 */

lumex::block_details::block_details (lumex::epoch const epoch_a, bool const is_send_a, bool const is_receive_a, bool const is_epoch_a) :
	epoch (epoch_a), is_send (is_send_a), is_receive (is_receive_a), is_epoch (is_epoch_a)
{
}

bool lumex::block_details::operator== (lumex::block_details const & other_a) const
{
	return epoch == other_a.epoch && is_send == other_a.is_send && is_receive == other_a.is_receive && is_epoch == other_a.is_epoch;
}

uint8_t lumex::block_details::packed () const
{
	std::bitset<8> result (static_cast<uint8_t> (epoch));
	result.set (7, is_send);
	result.set (6, is_receive);
	result.set (5, is_epoch);
	return static_cast<uint8_t> (result.to_ulong ());
}

void lumex::block_details::unpack (uint8_t details_a)
{
	constexpr std::bitset<8> epoch_mask{ 0b00011111 };
	auto as_bitset = static_cast<std::bitset<8>> (details_a);
	is_send = as_bitset.test (7);
	is_receive = as_bitset.test (6);
	is_epoch = as_bitset.test (5);
	epoch = static_cast<lumex::epoch> ((as_bitset & epoch_mask).to_ulong ());
}

void lumex::block_details::serialize (lumex::stream & stream_a) const
{
	lumex::write (stream_a, packed ());
}

bool lumex::block_details::deserialize (lumex::stream & stream_a)
{
	bool result (false);
	try
	{
		uint8_t packed{ 0 };
		lumex::read (stream_a, packed);
		unpack (packed);
	}
	catch (std::runtime_error &)
	{
		result = true;
	}

	return result;
}

void lumex::block_details::operator() (lumex::object_stream & obs) const
{
	obs.write ("epoch", epoch);
	obs.write ("is_send", is_send);
	obs.write ("is_receive", is_receive);
	obs.write ("is_epoch", is_epoch);
}

std::string lumex::state_subtype (lumex::block_details const details_a)
{
	debug_assert (details_a.is_epoch + details_a.is_receive + details_a.is_send <= 1);
	if (details_a.is_send)
	{
		return "send";
	}
	else if (details_a.is_receive)
	{
		return "receive";
	}
	else if (details_a.is_epoch)
	{
		return "epoch";
	}
	else
	{
		return "change";
	}
}

/*
 * block_sideband
 */

lumex::block_sideband::block_sideband (lumex::account const & account_a, lumex::amount const & balance_a, uint64_t const height_a, lumex::seconds_t const timestamp_a, lumex::block_details const & details_a, lumex::epoch const source_epoch_a, uint64_t const topo_height_a) :
	account (account_a),
	balance (balance_a),
	height (height_a),
	topo_height (topo_height_a),
	timestamp (timestamp_a),
	details (details_a),
	source_epoch (source_epoch_a)
{
}

lumex::block_sideband::block_sideband (lumex::account const & account_a, lumex::amount const & balance_a, uint64_t const height_a, lumex::seconds_t const timestamp_a, lumex::epoch const epoch_a, bool const is_send, bool const is_receive, bool const is_epoch, lumex::epoch const source_epoch_a, uint64_t const topo_height_a) :
	account (account_a),
	balance (balance_a),
	height (height_a),
	topo_height (topo_height_a),
	timestamp (timestamp_a),
	details (epoch_a, is_send, is_receive, is_epoch),
	source_epoch (source_epoch_a)
{
}

bool lumex::block_sideband::includes_account (lumex::block_type const type)
{
	return type != lumex::block_type::state && type != lumex::block_type::open;
}

bool lumex::block_sideband::includes_height (lumex::block_type const type)
{
	return type != lumex::block_type::open;
}

bool lumex::block_sideband::includes_balance (lumex::block_type const type)
{
	return type == lumex::block_type::receive || type == lumex::block_type::change || type == lumex::block_type::open;
}

bool lumex::block_sideband::includes_details (lumex::block_type const type)
{
	return type == lumex::block_type::state;
}

size_t lumex::block_sideband::size (lumex::block_type type)
{
	size_t result (0);
	if (includes_account (type))
	{
		result += sizeof (account);
	}
	if (includes_height (type))
	{
		result += sizeof (height);
	}
	if (includes_balance (type))
	{
		result += sizeof (balance);
	}
	result += sizeof (topo_height);
	result += sizeof (timestamp);
	if (includes_details (type))
	{
		static_assert (sizeof (lumex::epoch) == lumex::block_details::size (), "block_details is larger than the epoch enum");
		result += lumex::block_details::size () + sizeof (lumex::epoch);
	}
	return result;
}

void lumex::block_sideband::serialize (lumex::stream & stream_a, lumex::block_type type) const
{
	if (includes_account (type))
	{
		lumex::write (stream_a, account.bytes);
	}
	if (includes_height (type))
	{
		lumex::write (stream_a, boost::endian::native_to_big (height));
	}
	if (includes_balance (type))
	{
		lumex::write (stream_a, balance.bytes);
	}
	lumex::write (stream_a, boost::endian::native_to_big (topo_height));
	lumex::write (stream_a, boost::endian::native_to_big (timestamp));
	if (includes_details (type))
	{
		details.serialize (stream_a);
		lumex::write (stream_a, static_cast<uint8_t> (source_epoch));
	}
}

bool lumex::block_sideband::deserialize (lumex::stream & stream_a, lumex::block_type type)
{
	bool result (false);
	try
	{
		if (includes_account (type))
		{
			lumex::read (stream_a, account.bytes);
		}
		else
		{
			account.clear ();
		}
		if (includes_height (type))
		{
			lumex::read (stream_a, height);
			boost::endian::big_to_native_inplace (height);
		}
		else
		{
			height = 1;
		}
		if (includes_balance (type))
		{
			lumex::read (stream_a, balance.bytes);
		}
		else
		{
			balance.clear ();
		}
		lumex::read (stream_a, topo_height);
		boost::endian::big_to_native_inplace (topo_height);
		lumex::read (stream_a, timestamp);
		boost::endian::big_to_native_inplace (timestamp);
		if (includes_details (type))
		{
			result = details.deserialize (stream_a);
			uint8_t source_epoch_uint8_t{ 0 };
			lumex::read (stream_a, source_epoch_uint8_t);
			source_epoch = static_cast<lumex::epoch> (source_epoch_uint8_t);
		}
		else
		{
			details = {};
			source_epoch = lumex::epoch::epoch_0;
		}
	}
	catch (std::runtime_error &)
	{
		result = true;
	}

	return result;
}

void lumex::block_sideband::operator() (lumex::object_stream & obs) const
{
	obs.write ("account", account);
	obs.write ("balance", balance);
	obs.write ("height", height);
	obs.write ("timestamp", timestamp);
	obs.write ("topo_height", topo_height);
	obs.write ("source_epoch", source_epoch);
	obs.write ("details", details);
}

/*
 * block_sideband_v25
 */

size_t lumex::block_sideband_v25::size (lumex::block_type type)
{
	size_t result = sizeof (lumex::block_hash); // successor
	if (lumex::block_sideband::includes_account (type))
	{
		result += sizeof (lumex::account);
	}
	if (lumex::block_sideband::includes_height (type))
	{
		result += sizeof (uint64_t);
	}
	if (lumex::block_sideband::includes_balance (type))
	{
		result += sizeof (lumex::amount);
	}
	result += sizeof (uint64_t); // timestamp
	if (lumex::block_sideband::includes_details (type))
	{
		result += lumex::block_details::size () + sizeof (uint8_t); // details + source_epoch
	}
	return result;
}

void lumex::block_sideband_v25::serialize (lumex::stream & stream_a, lumex::block_type type) const
{
	lumex::write (stream_a, successor.bytes);
	if (lumex::block_sideband::includes_account (type))
	{
		lumex::write (stream_a, account.bytes);
	}
	if (lumex::block_sideband::includes_height (type))
	{
		lumex::write (stream_a, boost::endian::native_to_big (height));
	}
	if (lumex::block_sideband::includes_balance (type))
	{
		lumex::write (stream_a, balance.bytes);
	}
	lumex::write (stream_a, boost::endian::native_to_big (timestamp));
	if (lumex::block_sideband::includes_details (type))
	{
		details.serialize (stream_a);
		lumex::write (stream_a, static_cast<uint8_t> (source_epoch));
	}
}

bool lumex::block_sideband_v25::deserialize (lumex::stream & stream_a, lumex::block_type type)
{
	bool result (false);
	try
	{
		lumex::read (stream_a, successor.bytes);
		if (lumex::block_sideband::includes_account (type))
		{
			lumex::read (stream_a, account.bytes);
		}
		else
		{
			account.clear ();
		}
		if (lumex::block_sideband::includes_height (type))
		{
			lumex::read (stream_a, height);
			boost::endian::big_to_native_inplace (height);
		}
		else
		{
			height = 1;
		}
		if (lumex::block_sideband::includes_balance (type))
		{
			lumex::read (stream_a, balance.bytes);
		}
		else
		{
			balance.clear ();
		}
		lumex::read (stream_a, timestamp);
		boost::endian::big_to_native_inplace (timestamp);
		if (lumex::block_sideband::includes_details (type))
		{
			result = details.deserialize (stream_a);
			uint8_t source_epoch_uint8_t{ 0 };
			lumex::read (stream_a, source_epoch_uint8_t);
			source_epoch = static_cast<lumex::epoch> (source_epoch_uint8_t);
		}
		else
		{
			details = {};
			source_epoch = lumex::epoch::epoch_0;
		}
	}
	catch (std::runtime_error &)
	{
		result = true;
	}

	return result;
}
