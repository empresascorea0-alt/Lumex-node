#include <lumex/lib/assert.hpp>
#include <lumex/store/rocksdb/transaction_rocksdb.hpp>

/*
 * read_transaction_impl
 */

lumex::store::rocksdb::read_transaction_impl::read_transaction_impl (::rocksdb::DB * db_a, lumex::store::txn_callbacks txn_callbacks_a) :
	db{ db_a },
	txn_callbacks{ txn_callbacks_a }
{
	renew ();
}

lumex::store::rocksdb::read_transaction_impl::~read_transaction_impl ()
{
	reset ();
}

void lumex::store::rocksdb::read_transaction_impl::reset ()
{
	if (active)
	{
		db->ReleaseSnapshot (options.snapshot);
		txn_callbacks.txn_end (this);
		active = false;
	}
}

void lumex::store::rocksdb::read_transaction_impl::renew ()
{
	release_assert (db != nullptr);
	release_assert (!active);
	options.snapshot = db->GetSnapshot ();
	txn_callbacks.txn_start (this);
	active = true;
}

void * lumex::store::rocksdb::read_transaction_impl::get_handle () const
{
	return (void *)&options;
}

/*
 * write_transaction_impl
 */

lumex::store::rocksdb::write_transaction_impl::write_transaction_impl (::rocksdb::TransactionDB * db_a, lumex::store::txn_callbacks txn_callbacks_a) :
	db{ db_a },
	txn_callbacks{ txn_callbacks_a }
{
	renew ();
}

lumex::store::rocksdb::write_transaction_impl::~write_transaction_impl ()
{
	commit ();
}

void lumex::store::rocksdb::write_transaction_impl::commit ()
{
	if (active)
	{
		auto status = txn->Commit ();
		release_assert (status.ok (), "Unable to write to the RocksDB database", status.ToString ());
		delete txn;
		txn = nullptr;
		txn_callbacks.txn_end (this);
		active = false;
	}
}

void lumex::store::rocksdb::write_transaction_impl::renew ()
{
	debug_assert (check_no_write_tx ());
	release_assert (!active);
	release_assert (txn == nullptr);
	::rocksdb::TransactionOptions txn_options;
	txn_options.set_snapshot = true;
	txn = db->BeginTransaction (::rocksdb::WriteOptions (), txn_options);
	txn_callbacks.txn_start (this);
	active = true;
}

void * lumex::store::rocksdb::write_transaction_impl::get_handle () const
{
	return txn;
}

bool lumex::store::rocksdb::write_transaction_impl::contains (lumex::store::table table) const
{
	return true;
}

bool lumex::store::rocksdb::write_transaction_impl::check_no_write_tx () const
{
	std::vector<::rocksdb::Transaction *> transactions;
	db->GetAllPreparedTransactions (&transactions);
	return transactions.empty ();
}
