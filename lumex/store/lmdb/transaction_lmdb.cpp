#include <lumex/lib/assert.hpp>
#include <lumex/store/lmdb/common.hpp>
#include <lumex/store/lmdb/lmdb_env.hpp>
#include <lumex/store/lmdb/transaction_lmdb.hpp>

/*
 * read_transaction_impl
 */

lumex::store::lmdb::read_transaction_impl::read_transaction_impl (lumex::store::lmdb::env const & env_a, lumex::store::txn_callbacks txn_callbacks_a) :
	lumex::store::read_transaction_impl{ env_a.store_id },
	env{ env_a },
	txn_callbacks{ txn_callbacks_a }
{
	renew ();
}

lumex::store::lmdb::read_transaction_impl::~read_transaction_impl ()
{
	reset ();
}

void lumex::store::lmdb::read_transaction_impl::reset ()
{
	if (active)
	{
		mdb_txn_abort (handle);
		handle = nullptr;
		txn_callbacks.txn_end (this);
		active = false;
	}
}

void lumex::store::lmdb::read_transaction_impl::renew ()
{
	release_assert (!active);
	release_assert (handle == nullptr);
	auto status = mdb_txn_begin (env, nullptr, MDB_RDONLY, &handle);
	release_assert (success (status), error_string (status));
	txn_callbacks.txn_start (this);
	active = true;
}

void * lumex::store::lmdb::read_transaction_impl::get_handle () const
{
	return handle;
}

/*
 * write_transaction_impl
 */

lumex::store::lmdb::write_transaction_impl::write_transaction_impl (lumex::store::lmdb::env const & env_a, lumex::store::txn_callbacks txn_callbacks_a) :
	lumex::store::write_transaction_impl (env_a.store_id),
	env{ env_a },
	txn_callbacks{ txn_callbacks_a }
{
	renew ();
}

lumex::store::lmdb::write_transaction_impl::~write_transaction_impl ()
{
	commit ();
}

void lumex::store::lmdb::write_transaction_impl::commit ()
{
	if (active)
	{
		auto status = mdb_txn_commit (handle);
		release_assert (success (status), "Unable to write to the LMDB database", error_string (status));
		handle = nullptr;
		txn_callbacks.txn_end (this);
		active = false;
	}
}

void lumex::store::lmdb::write_transaction_impl::renew ()
{
	release_assert (!active);
	release_assert (handle == nullptr);
	auto status (mdb_txn_begin (env, nullptr, 0, &handle));
	release_assert (success (status), error_string (status));
	txn_callbacks.txn_start (this);
	active = true;
}

void * lumex::store::lmdb::write_transaction_impl::get_handle () const
{
	return handle;
}

bool lumex::store::lmdb::write_transaction_impl::contains (lumex::store::table table) const
{
	// LMDB locks on every write
	return true;
}
