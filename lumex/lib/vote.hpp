#pragma once

#include <lumex/lib/fwd.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/timer.hpp>
#include <lumex/lib/uniquer.hpp>

#include <boost/iterator/transform_iterator.hpp>
#include <boost/property_tree/ptree_fwd.hpp>

#include <vector>

namespace lumex
{
class vote final
{
public:
	vote () = default;
	vote (lumex::vote const &) = default;
	vote (bool & error, lumex::stream &);
	vote (lumex::account const &, lumex::raw_key const &, lumex::millis_t timestamp, uint8_t duration, std::vector<lumex::block_hash> const & hashes);

	void serialize (lumex::stream &) const;
	/**
	 * Deserializes a vote from the bytes in `stream'
	 * @returns true if there was an error
	 */
	bool deserialize (lumex::stream &);
	static std::size_t size (uint8_t count); // TODO: This name is confusing, vote size is number of hashes present, not the message size

	lumex::block_hash hash () const;
	lumex::block_hash full_hash () const;
	bool validate () const;

	bool operator== (lumex::vote const &) const;
	bool operator!= (lumex::vote const &) const;

	void serialize_json (boost::property_tree::ptree & tree) const;
	std::string to_json () const;
	std::string hashes_string () const;

	uint64_t timestamp () const;
	uint8_t duration_bits () const;
	std::chrono::milliseconds duration () const;
	bool is_final () const;

	static uint64_t constexpr timestamp_mask = { 0xffff'ffff'ffff'fff0ULL };
	static lumex::seconds_t constexpr timestamp_max = { 0xffff'ffff'ffff'fff0ULL };
	static uint64_t constexpr timestamp_min = { 0x0000'0000'0000'0010ULL };
	static uint8_t constexpr duration_max = { 0x0fu };

	static std::size_t constexpr max_hashes = 255;

	/* Check if timestamp represents a final vote */
	static bool is_final_timestamp (uint64_t timestamp);

public: // Payload
	// The hashes for which this vote directly covers
	std::vector<lumex::block_hash> hashes;
	// Account that's voting
	lumex::account account{ 0 };
	// Signature of timestamp + block hashes
	lumex::signature signature{ 0 };

private: // Payload
	// Vote timestamp (milliseconds since epoch)
	uint64_t timestamp_m{ 0 };

private:
	// Size of vote payload without hashes
	static std::size_t constexpr partial_size = sizeof (account) + sizeof (signature) + sizeof (timestamp_m);
	static std::string const hash_prefix;

	static uint64_t packed_timestamp (uint64_t timestamp, uint8_t duration);

public: // Logging
	void operator() (lumex::object_stream &) const;
};

using vote_uniquer = lumex::uniquer<lumex::block_hash, lumex::vote>;
}
