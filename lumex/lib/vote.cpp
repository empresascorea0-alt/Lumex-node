#include <lumex/crypto/blake2/blake2.h>
#include <lumex/lib/object_stream.hpp>
#include <lumex/lib/stream.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/lib/vote.hpp>

#include <boost/property_tree/json_parser.hpp>

lumex::vote::vote (bool & error_a, lumex::stream & stream_a)
{
	error_a = deserialize (stream_a);
}

lumex::vote::vote (lumex::account const & account_a, lumex::raw_key const & prv_a, uint64_t timestamp_a, uint8_t duration, std::vector<lumex::block_hash> const & hashes) :
	hashes{ hashes },
	timestamp_m{ packed_timestamp (timestamp_a, duration) },
	account{ account_a }
{
	debug_assert (hashes.size () <= max_hashes);

	signature = lumex::sign_message (prv_a, account_a, hash ());
}

void lumex::vote::serialize (lumex::stream & stream_a) const
{
	debug_assert (hashes.size () <= max_hashes);

	write (stream_a, account);
	write (stream_a, signature);
	write (stream_a, boost::endian::native_to_little (timestamp_m));
	for (auto const & hash : hashes)
	{
		write (stream_a, hash);
	}
}

bool lumex::vote::deserialize (lumex::stream & stream_a)
{
	auto error = false;
	try
	{
		lumex::read (stream_a, account.bytes);
		lumex::read (stream_a, signature.bytes);
		lumex::read (stream_a, timestamp_m);

		while (stream_a.in_avail () > 0 && hashes.size () < max_hashes)
		{
			lumex::block_hash block_hash;
			lumex::read (stream_a, block_hash);
			hashes.push_back (block_hash);
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

std::size_t lumex::vote::size (uint8_t count)
{
	debug_assert (count <= max_hashes);
	return partial_size + count * sizeof (lumex::block_hash);
}

std::string const lumex::vote::hash_prefix = "vote ";

lumex::block_hash lumex::vote::hash () const
{
	lumex::block_hash result;
	blake2b_state hash;
	blake2b_init (&hash, sizeof (result.bytes));
	blake2b_update (&hash, hash_prefix.data (), hash_prefix.size ());
	for (auto const & block_hash : hashes)
	{
		blake2b_update (&hash, block_hash.bytes.data (), sizeof (block_hash.bytes));
	}
	union
	{
		uint64_t qword;
		std::array<uint8_t, 8> bytes;
	};
	qword = timestamp_m;
	blake2b_update (&hash, bytes.data (), sizeof (bytes));
	blake2b_final (&hash, result.bytes.data (), sizeof (result.bytes));
	return result;
}

lumex::block_hash lumex::vote::full_hash () const
{
	lumex::block_hash result;
	blake2b_state state;
	blake2b_init (&state, sizeof (result.bytes));
	blake2b_update (&state, hash ().bytes.data (), sizeof (hash ().bytes));
	blake2b_update (&state, account.bytes.data (), sizeof (account.bytes.data ()));
	blake2b_update (&state, signature.bytes.data (), sizeof (signature.bytes.data ()));
	blake2b_final (&state, result.bytes.data (), sizeof (result.bytes));
	return result;
}

bool lumex::vote::validate () const
{
	return lumex::validate_message (account, hash (), signature);
}

bool lumex::vote::operator== (lumex::vote const & other_a) const
{
	return timestamp_m == other_a.timestamp_m && hashes == other_a.hashes && account == other_a.account && signature == other_a.signature;
}

bool lumex::vote::operator!= (lumex::vote const & other_a) const
{
	return !(*this == other_a);
}

/**
 * Returns the timestamp of the vote (with the duration bits masked, set to zero)
 * If it is a final vote, all the bits including duration bits are returned as they are, all FF
 */
uint64_t lumex::vote::timestamp () const
{
	return (timestamp_m == std::numeric_limits<uint64_t>::max ())
	? timestamp_m // final vote
	: (timestamp_m & timestamp_mask);
}

uint8_t lumex::vote::duration_bits () const
{
	// Duration field is specified in the 4 low-order bits of the timestamp.
	// This makes the timestamp have a minimum granularity of 16ms
	// The duration is specified as 2^(duration + 4) giving it a range of 16-524,288ms in power of two increments
	auto result = timestamp_m & ~timestamp_mask;
	debug_assert (result < 16);
	return static_cast<uint8_t> (result);
}

std::chrono::milliseconds lumex::vote::duration () const
{
	return std::chrono::milliseconds{ 1u << (duration_bits () + 4) };
}

bool lumex::vote::is_final () const
{
	return is_final_timestamp (timestamp_m);
}

void lumex::vote::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("account", account.to_account ());
	tree.put ("signature", signature.to_string ());
	tree.put ("sequence", std::to_string (timestamp ()));
	tree.put ("timestamp", std::to_string (timestamp ()));
	tree.put ("duration", std::to_string (duration_bits ()));
	boost::property_tree::ptree blocks_tree;
	for (auto const & hash : hashes)
	{
		boost::property_tree::ptree entry;
		entry.put ("", hash.to_string ());
		blocks_tree.push_back (std::make_pair ("", entry));
	}
	tree.add_child ("blocks", blocks_tree);
}

std::string lumex::vote::to_json () const
{
	std::stringstream stream;
	boost::property_tree::ptree tree;
	serialize_json (tree);
	boost::property_tree::write_json (stream, tree);
	return stream.str ();
}

std::string lumex::vote::hashes_string () const
{
	return lumex::util::join (hashes, ", ", [] (auto const & hash) {
		return hash.to_string ();
	});
}

uint64_t lumex::vote::packed_timestamp (uint64_t timestamp, uint8_t duration)
{
	debug_assert (duration <= duration_max && "Invalid duration");
	debug_assert ((!(timestamp == timestamp_max) || (duration == duration_max)) && "Invalid final vote");
	return (timestamp & timestamp_mask) | duration;
}

bool lumex::vote::is_final_timestamp (uint64_t timestamp)
{
	return timestamp == std::numeric_limits<uint64_t>::max ();
}

void lumex::vote::operator() (lumex::object_stream & obs) const
{
	obs.write ("account", account);
	obs.write ("final", is_final_timestamp (timestamp_m));
	obs.write ("timestamp", timestamp_m);
	obs.write_range ("hashes", hashes);
}
