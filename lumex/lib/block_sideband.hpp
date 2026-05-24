#pragma once

#include <lumex/lib/epoch.hpp>
#include <lumex/lib/fwd.hpp>
#include <lumex/lib/numbers.hpp>

#include <cstdint>
#include <memory>

namespace lumex
{
class block_details
{
	static_assert (std::is_same<std::underlying_type<lumex::epoch>::type, uint8_t> (), "Epoch enum is not the proper type");
	static_assert (static_cast<uint8_t> (lumex::epoch::max) < (1 << 5), "Epoch max is too large for the sideband");

public:
	block_details () = default;
	block_details (lumex::epoch epoch, bool is_send, bool is_receive, bool is_epoch);

	static constexpr size_t size ()
	{
		return 1;
	}
	bool operator== (block_details const &) const;
	void serialize (lumex::stream &) const;
	bool deserialize (lumex::stream &);

public:
	lumex::epoch epoch{ lumex::epoch::epoch_0 };
	bool is_send{ false };
	bool is_receive{ false };
	bool is_epoch{ false };

private:
	uint8_t packed () const;
	void unpack (uint8_t);

public: // Logging
	void operator() (lumex::object_stream &) const;
};

std::string state_subtype (lumex::block_details);

// WARNING: Sideband layout is block-type dependent. Not all fields below are serialized for every block type
// Use lumex::block accessors (e.g. block.account(), block.balance(), block.previous()) for canonical values
class block_sideband final
{
public:
	block_sideband () = default;
	block_sideband (lumex::account const & account, lumex::amount const & balance, uint64_t height, lumex::seconds_t timestamp, lumex::block_details const & details, lumex::epoch source_epoch, uint64_t topo_height = 0);
	block_sideband (lumex::account const & account, lumex::amount const & balance, uint64_t height, lumex::seconds_t timestamp, lumex::epoch epoch, bool is_send, bool is_receive, bool is_epoch, lumex::epoch source_epoch, uint64_t topo_height = 0);

	void serialize (lumex::stream &, lumex::block_type) const;
	bool deserialize (lumex::stream &, lumex::block_type);

	static size_t size (lumex::block_type);

	static bool includes_account (lumex::block_type);
	static bool includes_height (lumex::block_type);
	static bool includes_balance (lumex::block_type);
	static bool includes_details (lumex::block_type);

public:
	lumex::account account{}; // Not serialized for state/open blocks
	lumex::amount balance{ 0 }; // Serialized only for receive/change/open blocks
	uint64_t height{ 0 }; // Not serialized for open blocks (deserialized as 1)
	uint64_t topo_height{ 0 };
	uint64_t timestamp{ 0 };
	lumex::block_details details; // Serialized only for state blocks
	lumex::epoch source_epoch{ lumex::epoch::epoch_0 }; // Serialized only for state blocks

public: // Logging
	void operator() (lumex::object_stream &) const;
};

/**
 * Snapshot of the block sideband format at v25 (includes successor field).
 * Used during v24->v25 and v25->v26 database migrations.
 */
class block_sideband_v25 final
{
public:
	block_sideband_v25 () = default;

	void serialize (lumex::stream &, lumex::block_type) const;
	bool deserialize (lumex::stream &, lumex::block_type);
	static size_t size (lumex::block_type);

public:
	lumex::block_hash successor{ 0 };
	lumex::account account{};
	lumex::amount balance{ 0 };
	uint64_t height{ 0 };
	uint64_t timestamp{ 0 };
	lumex::block_details details;
	lumex::epoch source_epoch{ lumex::epoch::epoch_0 };
};
}
