#pragma once

#include <lumex/lib/errors.hpp>
#include <lumex/lib/fwd.hpp>
#include <lumex/lib/result.hpp>
#include <lumex/store/common.hpp>
#include <lumex/store/db_val.hpp>
#include <lumex/store/iterator.hpp>
#include <lumex/store/meta.hpp>
#include <lumex/store/tables.hpp>
#include <lumex/store/transaction.hpp>
#include <lumex/store/txn_tracking.hpp>

#include <boost/property_tree/ptree_fwd.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace lumex
{
enum class error_backend
{
	generic = 1,
	db_not_found,
	table_not_found,
	failure,
};
}
REGISTER_ERROR_CODES (lumex, error_backend)

namespace lumex::store
{
using backend_version_t = uint64_t;

struct backend_meta
{
	backend_version_t version;
};

using column_definition = std::pair<lumex::store::table, std::string>;
using column_schema = std::set<column_definition>;

struct copy_progress
{
	size_t current_table_index;
	size_t total_tables;
	lumex::store::table table;
	std::string table_name;
	uint64_t entries_copied;
	uint64_t total_entries;
};

using copy_progress_callback = std::function<void (copy_progress const &)>;

/**
 * Polymorphic backend interface for key-value database operations.
 * This provides the minimal interface that database backends (LMDB, RocksDB) must implement.
 * All business logic should be in common implementation classes that use this interface.
 */
class backend
{
public:
	backend (lumex::logger &, lumex::store::txn_tracking_config const &);
	virtual ~backend ();

	std::optional<backend_meta> fetch_meta ();

	void open (column_schema, lumex::store::open_mode mode);
	void create (column_schema, lumex::store::backend_version_t version);
	void close ();

	// Basic CRUD operations
	virtual int get (lumex::store::transaction const &, lumex::store::table, lumex::store::db_val const & key, lumex::store::db_val & value) const = 0;
	virtual int put (lumex::store::write_transaction const &, lumex::store::table, lumex::store::db_val const & key, lumex::store::db_val const & value) = 0;
	virtual int del (lumex::store::write_transaction const &, lumex::store::table, lumex::store::db_val const & key) = 0;
	virtual bool exists (lumex::store::transaction const &, lumex::store::table, lumex::store::db_val const & key) const = 0;

	// Table operations
	bool empty (lumex::store::transaction const &) const; // Checks if all tables are empty
	bool empty (lumex::store::transaction const &, lumex::store::table) const;
	virtual uint64_t count (lumex::store::transaction const &, lumex::store::table) const = 0;
	virtual bool count_is_exact (lumex::store::table) const = 0; // Returns true if count() returns exact value, false if estimate
	uint64_t count_exact (lumex::store::transaction const &, lumex::store::table) const; // Exact count via iteration (always accurate)
	virtual int clear (lumex::store::table) = 0; // Empties the table but keeps it
	virtual bool drop_table (std::string const & name) = 0; // Deletes the table entirely
	virtual bool table_exists (std::string const & name) const = 0;

	// Iterator operations
	virtual lumex::store::iterator begin (lumex::store::transaction const &, lumex::store::table) const = 0;
	virtual lumex::store::iterator begin (lumex::store::transaction const &, lumex::store::table, lumex::store::db_val const & key) const = 0;
	virtual lumex::store::iterator end (lumex::store::transaction const &, lumex::store::table) const = 0;

	// Parallel iteration
	void for_each_par (lumex::store::table,
	std::function<void (read_transaction const &, iterator, iterator)> const & action) const;

	// Copy all tables to another backend
	void copy_to (backend & destination,
	copy_progress_callback progress_callback = nullptr,
	size_t batch_size = 10000) const;

	virtual void copy_with_compaction (std::filesystem::path const & destination) = 0;
	virtual void backup () = 0;

	// Diagnostics (optional, backend-specific)
	virtual void collect_txn_tracker (boost::property_tree::ptree &, std::chrono::milliseconds min_read_time, std::chrono::milliseconds min_write_time) const;
	virtual void collect_memory_stats (boost::property_tree::ptree &) const;

	// Status checking
	virtual bool success (int status) const = 0;
	virtual bool not_found (int status) const = 0;
	virtual std::string error_string (int status) const = 0;

	// Transaction management
	virtual lumex::store::read_transaction tx_begin_read () const = 0;
	virtual lumex::store::write_transaction tx_begin_write () = 0;

	// Helper methods
	void release_assert_success (int status) const;

	virtual std::string get_vendor () const = 0;
	virtual std::string get_database_path () const = 0;

	std::optional<lumex::store::open_mode> get_mode () const;
	column_schema get_schema () const;
	backend_meta get_meta () const;

	lumex::store::backend_version_t get_version (lumex::store::transaction const &) const;
	void set_version (lumex::store::write_transaction const &, lumex::store::backend_version_t version);

protected:
	virtual void open_impl (column_schema, lumex::store::open_mode) = 0;
	virtual void close_impl () = 0;

private:
	void load_meta ();

protected: // Transaction tracking
	mutable std::unique_ptr<lumex::store::txn_tracker> tracker;
	lumex::store::txn_callbacks txn_tracking_callbacks () const;

private:
	bool is_open{ false };
	lumex::store::open_mode current_mode{};
	std::optional<backend_meta> current_meta{};
	column_schema current_schema{};

	lumex::store::meta_view meta{ *this };

public:
	static lumex::store::column_schema const schema_meta;
};

inline void backend::release_assert_success (int status) const
{
	if (!success (status))
	{
		release_assert (false, error_string (status));
	}
}
}