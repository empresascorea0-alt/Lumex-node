#pragma once

#include <lumex/lib/fwd.hpp>
#include <lumex/lib/numbers.hpp>

#include <memory>

namespace lumex
{
enum class no_value
{
	dummy
};

class unchecked_key final
{
public:
	unchecked_key () = default;
	explicit unchecked_key (lumex::hash_or_account const & dependency);
	unchecked_key (lumex::hash_or_account const &, lumex::block_hash const &);
	unchecked_key (lumex::uint512_union const &);
	bool deserialize (lumex::stream &);
	bool operator== (lumex::unchecked_key const &) const;
	bool operator< (lumex::unchecked_key const &) const;
	lumex::block_hash const & key () const;
	lumex::block_hash previous{ 0 };
	lumex::block_hash hash{ 0 };
};

class topo_key final
{
public:
	topo_key () = default;
	topo_key (uint64_t topo_height_a, lumex::block_hash const & hash_a) :
		topo_height{ topo_height_a },
		hash{ hash_a }
	{
	}

	auto operator<=> (topo_key const &) const = default;

	void serialize (lumex::stream &) const;
	bool deserialize (lumex::stream &);

public:
	uint64_t topo_height{ 0 };
	lumex::block_hash hash{ 0 };
};

/**
 * Information on an unchecked block
 */
class unchecked_info final
{
public:
	unchecked_info () = default;
	unchecked_info (std::shared_ptr<lumex::block> const &);
	void serialize (lumex::stream &) const;
	bool deserialize (lumex::stream &);
	lumex::seconds_t modified () const;
	std::shared_ptr<lumex::block> block;

private:
	/** Seconds since posix epoch */
	uint64_t modified_m{ 0 };
};

class block_info final
{
public:
	block_info () = default;
	block_info (lumex::account const &, lumex::amount const &);
	lumex::account account{};
	lumex::amount balance{ 0 };
};

class confirmation_height_info final
{
public:
	confirmation_height_info () = default;
	confirmation_height_info (uint64_t, lumex::block_hash const &);

	auto operator<=> (confirmation_height_info const &) const = default;

	void serialize (lumex::stream &) const;
	bool deserialize (lumex::stream &);

	/** height of the cemented frontier */
	uint64_t height{};

	/** hash of the highest cemented block, the cemented/confirmed frontier */
	lumex::block_hash frontier{};
};

namespace confirmation_height
{
	/** When the uncemented count (block count - cemented count) is less than this use the unbounded processor */
	uint64_t const unbounded_cutoff{ 16384 };
}

enum class block_status
{
	invalid, // Status is unknown, block is not processed yet (default value)
	progress, // Hasn't been seen before, signed correctly
	bad_signature, // Signature was bad, forged or transmission error
	old, // Already seen and was valid
	negative_spend, // Malicious attempt to spend a negative amount
	fork, // Malicious fork based on previous
	unreceivable, // Source block doesn't exist, has already been received, or requires an account upgrade (epoch blocks)
	gap_previous, // Block marked as previous is unknown
	gap_source, // Block marked as source is unknown
	gap_epoch_open_pending, // Block marked as pending blocks required for epoch open block are unknown
	opened_burn_account, // Block attempts to open the burn account
	balance_mismatch, // Balance and amount delta don't match
	representative_mismatch, // Representative is changed when it is not allowed
	block_position, // This block cannot follow the previous block
	insufficient_work // Insufficient work for this block, even though it passed the minimal validation
};

std::string_view to_string (block_status);
lumex::stat::detail to_stat_detail (block_status);

enum class tally_result
{
	vote,
	changed,
	confirm
};

lumex::wallet_id random_wallet_id ();
}
