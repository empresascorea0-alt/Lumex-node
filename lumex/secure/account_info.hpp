#pragma once

#include <lumex/lib/epoch.hpp>
#include <lumex/lib/fwd.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/timer.hpp>

namespace lumex
{
/**
 * Latest information about an account
 */
class account_info final
{
public:
	account_info () = default;
	account_info (lumex::block_hash const &, lumex::account const &, lumex::block_hash const &, lumex::amount const &, lumex::seconds_t modified, uint64_t, epoch);
	bool deserialize (lumex::stream &);
	bool operator== (lumex::account_info const &) const;
	bool operator!= (lumex::account_info const &) const;
	size_t db_size () const;
	lumex::epoch epoch () const;
	lumex::block_hash head{ 0 };
	lumex::account representative{};
	lumex::block_hash open_block{ 0 };
	lumex::amount balance{ 0 };
	/** Seconds since posix epoch */
	lumex::seconds_t modified{ 0 };
	uint64_t block_count{ 0 };
	lumex::epoch epoch_m{ lumex::epoch::epoch_0 };
};

/**
 * This is a snapshot of the account_info table at v22 which needs to be read for the v22 to v23 upgrade
 */
class account_info_v22 final
{
public:
	account_info_v22 () = default;
	size_t db_size () const;
	bool deserialize (lumex::stream &);
	lumex::block_hash head{ 0 };
	lumex::account representative{};
	lumex::block_hash open_block{ 0 };
	lumex::amount balance{ 0 };
	/** Seconds since posix epoch */
	lumex::seconds_t modified{ 0 };
	uint64_t block_count{ 0 };
	lumex::epoch epoch_m{ lumex::epoch::epoch_0 };
};
}
