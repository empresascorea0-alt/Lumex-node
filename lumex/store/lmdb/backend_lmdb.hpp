#pragma once

#include <lumex/lib/lmdbconfig.hpp>
#include <lumex/store/backend.hpp>
#include <lumex/store/lmdb/lmdb_env.hpp>
#include <lumex/store/lmdb/transaction_lmdb.hpp>

#include <unordered_map>

namespace lumex::store::lmdb
{
/**
 * LMDB implementation of the backend interface.
 * Provides low-level database operations using LMDB.
 */
class backend_lmdb : public lumex::store::backend
{
public:
	backend_lmdb (std::filesystem::path const & path, lumex::lmdb_config const & config, lumex::logger & logger, lumex::store::txn_tracking_config const & txn_tracking_config = {});
	~backend_lmdb () override;

	int get (lumex::store::transaction const &, lumex::store::table, lumex::store::db_val const & key, lumex::store::db_val & value) const override;
	int put (lumex::store::write_transaction const &, lumex::store::table, lumex::store::db_val const & key, lumex::store::db_val const & value) override;
	int del (lumex::store::write_transaction const &, lumex::store::table, lumex::store::db_val const & key) override;
	bool exists (lumex::store::transaction const &, lumex::store::table, lumex::store::db_val const & key) const override;

	uint64_t count (lumex::store::transaction const &, lumex::store::table) const override;
	bool count_is_exact (lumex::store::table) const override;
	int clear (lumex::store::table) override;
	bool drop_table (std::string const & name) override;
	bool table_exists (std::string const & name) const override;

	lumex::store::iterator begin (lumex::store::transaction const &, lumex::store::table) const override;
	lumex::store::iterator begin (lumex::store::transaction const &, lumex::store::table, lumex::store::db_val const & key) const override;
	lumex::store::iterator end (lumex::store::transaction const &, lumex::store::table) const override;

	bool success (int status) const override;
	bool not_found (int status) const override;
	std::string error_string (int status) const override;

	lumex::store::read_transaction tx_begin_read () const override;
	lumex::store::write_transaction tx_begin_write () override;

	void copy_with_compaction (std::filesystem::path const & destination) override;
	void backup () override;

	void collect_memory_stats (boost::property_tree::ptree &) const override;

	std::string get_vendor () const override;
	std::string get_database_path () const override;

protected:
	void open_impl (column_schema schema, lumex::store::open_mode mode) override;
	void close_impl () override;

private:
	std::filesystem::path const database_path;
	lumex::lmdb_config config;

	std::unique_ptr<lumex::store::lmdb::env> env;
	std::unordered_map<lumex::store::table, lumex::store::lmdb::env::table_handle> table_handles;

	lumex::store::lmdb::env::table_handle table_to_dbi (lumex::store::table) const;
	void open_table (MDB_txn *, lumex::store::table, std::string const & name, unsigned flags);
};
}
