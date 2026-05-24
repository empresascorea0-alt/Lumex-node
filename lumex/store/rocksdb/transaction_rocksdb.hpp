#pragma once

#include <lumex/store/transaction.hpp>
#include <lumex/store/txn_tracking.hpp>

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/utilities/transaction.h>
#include <rocksdb/utilities/transaction_db.h>

namespace lumex::store::rocksdb
{
class read_transaction_impl final : public lumex::store::read_transaction_impl
{
public:
	explicit read_transaction_impl (::rocksdb::DB * db, lumex::store::txn_callbacks txn_callbacks = {});
	~read_transaction_impl () override;

	void reset () override;
	void renew () override;
	void * get_handle () const override;

private:
	::rocksdb::DB * db;
	lumex::store::txn_callbacks txn_callbacks;
	::rocksdb::ReadOptions options{};
	bool active{ false };
};

class write_transaction_impl final : public lumex::store::write_transaction_impl
{
public:
	explicit write_transaction_impl (::rocksdb::TransactionDB * db_a, lumex::store::txn_callbacks txn_callbacks = {});
	~write_transaction_impl () override;

	void commit () override;
	void renew () override;
	void * get_handle () const override;
	bool contains (lumex::store::table) const override;

private:
	bool check_no_write_tx () const;

	::rocksdb::TransactionDB * db;
	lumex::store::txn_callbacks txn_callbacks;
	::rocksdb::Transaction * txn{ nullptr };
	bool active{ false };
};
}
