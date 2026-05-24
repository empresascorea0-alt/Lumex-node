#pragma once

#include <lumex/lib/rocksdbconfig.hpp>
#include <lumex/store/backend.hpp>
#include <lumex/store/txn_tracking.hpp>

#include <functional>
#include <map>
#include <unordered_map>

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/table.h>
#include <rocksdb/utilities/transaction_db.h>

namespace lumex::store::rocksdb
{
/**
 * RocksDB implementation of the backend interface.
 */
class backend_rocksdb : public lumex::store::backend
{
public:
	backend_rocksdb (std::filesystem::path const & path, lumex::rocksdb_config const & config, lumex::logger & logger, lumex::store::txn_tracking_config const & txn_tracking_config = {});
	~backend_rocksdb () override;

	int get (lumex::store::transaction const &, lumex::store::table, lumex::store::db_val const & key, lumex::store::db_val & value) const override;
	int put (lumex::store::write_transaction const &, lumex::store::table, lumex::store::db_val const & key, lumex::store::db_val const & value) override;
	int del (lumex::store::write_transaction const &, lumex::store::table, lumex::store::db_val const & key) override;
	bool exists (lumex::store::transaction const &, lumex::store::table, lumex::store::db_val const & key) const override;

	// WARNING: count() may return estimates for some tables
	// Use count_is_exact() to check, or use empty() for reliable emptiness checks
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
	void open_db (std::filesystem::path const & path, lumex::store::open_mode mode, ::rocksdb::Options const & options, std::vector<::rocksdb::ColumnFamilyDescriptor> column_families);

	::rocksdb::ColumnFamilyHandle * table_to_column_family (lumex::store::table) const;
	::rocksdb::ColumnFamilyHandle * get_column_family (std::string const & name) const;
	bool column_family_exists (std::string const & name) const;

	::rocksdb::Options get_db_options (lumex::store::open_mode mode);
	::rocksdb::BlockBasedTableOptions get_table_options () const;
	::rocksdb::ColumnFamilyOptions get_cf_options (std::string const & cf_name) const;

private:
	std::filesystem::path const database_path;
	lumex::rocksdb_config const config;

	std::unique_ptr<::rocksdb::DB> db;
	::rocksdb::TransactionDB * transaction_db{ nullptr };
	std::vector<std::unique_ptr<::rocksdb::ColumnFamilyHandle>> handles;
	std::map<lumex::store::table, ::rocksdb::ColumnFamilyHandle *> table_handles;
	std::map<std::string, lumex::store::table> name_to_table;

public: // Tombstone management
	class tombstone_info
	{
	public:
		tombstone_info (uint64_t num, uint64_t max_a);
		std::atomic<uint64_t> num_since_last_flush;
		uint64_t const max;
	};
	std::unordered_map<lumex::store::table, tombstone_info> const & get_tombstone_map () const
	{
		return tombstone_map;
	}

private:
	std::unordered_map<lumex::store::table, tombstone_info> tombstone_map;

	void generate_tombstone_map ();
	void flush_tombstones_check (lumex::store::table);
	void flush_table (lumex::store::table);
	void on_flush (::rocksdb::FlushJobInfo const & flush_info);
};
}
