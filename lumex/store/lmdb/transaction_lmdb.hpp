#pragma once

#include <lumex/lib/id_dispenser.hpp>
#include <lumex/store/transaction.hpp>
#include <lumex/store/txn_tracking.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace lumex::store::lmdb
{
class env;

class read_transaction_impl final : public lumex::store::read_transaction_impl
{
public:
	explicit read_transaction_impl (lumex::store::lmdb::env const &, lumex::store::txn_callbacks txn_callbacks = {});
	~read_transaction_impl () override;

	void reset () override;
	void renew () override;
	void * get_handle () const override;

private:
	lumex::store::lmdb::env const & env;
	lumex::store::txn_callbacks txn_callbacks;
	MDB_txn * handle{ nullptr };
	bool active{ false };
};

class write_transaction_impl final : public lumex::store::write_transaction_impl
{
public:
	explicit write_transaction_impl (lumex::store::lmdb::env const &, lumex::store::txn_callbacks txn_callbacks = {});
	~write_transaction_impl () override;

	void commit () override;
	void renew () override;
	void * get_handle () const override;
	bool contains (lumex::store::table) const override;

private:
	lumex::store::lmdb::env const & env;
	lumex::store::txn_callbacks txn_callbacks;
	MDB_txn * handle{ nullptr };
	bool active{ false };
};
}
