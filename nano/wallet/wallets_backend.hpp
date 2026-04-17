#pragma once

#include <nano/lib/fwd.hpp>
#include <nano/store/db_val.hpp>
#include <nano/store/fwd.hpp>
#include <nano/store/iterator.hpp>
#include <nano/store/transaction.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace nano::wallet
{
class wallet_handle;

class wallets_backend
{
public:
	virtual ~wallets_backend () = default;

	virtual nano::store::read_transaction tx_begin_read () const = 0;
	virtual nano::store::write_transaction tx_begin_write () = 0;

	// Per-wallet sub-tables
	virtual wallet_handle wallet_open_or_create (nano::store::write_transaction const &, std::string const & wallet_id_hex) = 0;
	virtual void wallet_drop (nano::store::write_transaction const &, wallet_handle &) = 0;

	// KV on a sub-table. Keys and values are opaque byte ranges; callers convert to/from typed values
	virtual std::optional<nano::store::db_val> entry_get (nano::store::transaction const &, wallet_handle const &, nano::store::db_val const & key) const = 0;
	virtual void entry_put (nano::store::write_transaction const &, wallet_handle const &, nano::store::db_val const & key, nano::store::db_val const & value) = 0;
	virtual void entry_del (nano::store::write_transaction const &, wallet_handle const &, nano::store::db_val const & key) = 0;
	virtual bool entry_exists (nano::store::transaction const &, wallet_handle const &, nano::store::db_val const & key) const = 0;

	// Iteration over entries within a single wallet sub-table
	virtual nano::store::iterator entries_begin (nano::store::transaction const &, wallet_handle const &) const = 0;
	virtual nano::store::iterator entries_begin (nano::store::transaction const &, wallet_handle const &, nano::store::db_val const & key) const = 0;
	virtual nano::store::iterator entries_end (nano::store::transaction const &, wallet_handle const &) const = 0;

	// Iteration over the wallet index (lists all wallet sub-tables). Keys are backend-specific and the caller decodes them
	// The range may also contain entries for non-wallet sub-tables (e.g. `send_action_ids` on LMDB)
	// Callers must filter keys that don't parse as wallet ids
	virtual nano::store::iterator index_begin (nano::store::transaction const &) const = 0;
	virtual nano::store::iterator index_begin (nano::store::transaction const &, nano::store::db_val const & key) const = 0;
	virtual nano::store::iterator index_end (nano::store::transaction const &) const = 0;

	// Send action IDs (persistent across restart; dedupes send_action retries)
	virtual std::optional<nano::store::db_val> send_action_id_get (nano::store::transaction const &, nano::store::db_val const & id) const = 0;
	virtual bool send_action_id_put (nano::store::write_transaction const &, nano::store::db_val const & id, nano::store::db_val const & value) = 0;
	virtual void send_action_ids_clear (nano::store::write_transaction const &) = 0;

	// Environment
	virtual std::filesystem::path database_path () const = 0;
	virtual void backup (nano::logger &) const = 0;
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
