#pragma once

#include <lumex/lib/fwd.hpp>
#include <lumex/store/db_val.hpp>
#include <lumex/store/fwd.hpp>
#include <lumex/store/iterator.hpp>
#include <lumex/store/transaction.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace lumex::wallet
{
class wallet_handle;

class wallets_backend
{
public:
	virtual ~wallets_backend () = default;

	virtual lumex::store::read_transaction tx_begin_read () const = 0;
	virtual lumex::store::write_transaction tx_begin_write () = 0;

	// Per-wallet sub-tables
	virtual wallet_handle wallet_open_or_create (lumex::store::write_transaction const &, std::string const & wallet_id_hex) = 0;
	virtual void wallet_drop (lumex::store::write_transaction const &, wallet_handle &) = 0;

	// KV on a sub-table. Keys and values are opaque byte ranges; callers convert to/from typed values
	virtual std::optional<lumex::store::db_val> entry_get (lumex::store::transaction const &, wallet_handle const &, lumex::store::db_val const & key) const = 0;
	virtual void entry_put (lumex::store::write_transaction const &, wallet_handle const &, lumex::store::db_val const & key, lumex::store::db_val const & value) = 0;
	virtual void entry_del (lumex::store::write_transaction const &, wallet_handle const &, lumex::store::db_val const & key) = 0;
	virtual bool entry_exists (lumex::store::transaction const &, wallet_handle const &, lumex::store::db_val const & key) const = 0;

	// Iteration over entries within a single wallet sub-table
	virtual lumex::store::iterator entries_begin (lumex::store::transaction const &, wallet_handle const &) const = 0;
	virtual lumex::store::iterator entries_begin (lumex::store::transaction const &, wallet_handle const &, lumex::store::db_val const & key) const = 0;
	virtual lumex::store::iterator entries_end (lumex::store::transaction const &, wallet_handle const &) const = 0;

	// Iteration over the wallet index (lists all wallet sub-tables). Keys are backend-specific and the caller decodes them
	// The range may also contain entries for non-wallet sub-tables (e.g. `send_action_ids` on LMDB)
	// Callers must filter keys that don't parse as wallet ids
	virtual lumex::store::iterator index_begin (lumex::store::transaction const &) const = 0;
	virtual lumex::store::iterator index_begin (lumex::store::transaction const &, lumex::store::db_val const & key) const = 0;
	virtual lumex::store::iterator index_end (lumex::store::transaction const &) const = 0;

	// Send action IDs (persistent across restart; dedupes send_action retries)
	virtual std::optional<lumex::store::db_val> send_action_id_get (lumex::store::transaction const &, lumex::store::db_val const & id) const = 0;
	virtual bool send_action_id_put (lumex::store::write_transaction const &, lumex::store::db_val const & id, lumex::store::db_val const & value) = 0;
	virtual void send_action_ids_clear (lumex::store::write_transaction const &) = 0;

	// Environment
	virtual std::filesystem::path database_path () const = 0;
	virtual void backup (lumex::logger &) const = 0;
};

class wallet_handle
{
public:
	wallet_handle () = default;
	explicit wallet_handle (uint64_t value_a) :
		value_m{ value_a }
	{
	}

	uint64_t opaque () const
	{
		return value_m;
	}

	bool valid () const
	{
		return value_m != 0;
	}

	explicit operator bool () const
	{
		return valid ();
	}

	auto operator<=> (wallet_handle const &) const = default;

private:
	uint64_t value_m{ 0 };
};
}
