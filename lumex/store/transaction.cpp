#include <lumex/lib/thread_roles.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/store/transaction.hpp>

/*
 * transaction_impl
 */

lumex::store::transaction_impl::transaction_impl (lumex::id_dispenser::id_t const store_id_a) :
	store_id{ store_id_a }
{
	debug_assert (!lumex::thread_role::is_network_io (), "database operations are not allowed to run on network IO threads");
}

/*
 * read_transaction_impl
 */

lumex::store::read_transaction_impl::read_transaction_impl (lumex::id_dispenser::id_t const store_id_a) :
	transaction_impl (store_id_a)
{
}

/*
 * write_transaction_impl
 */

lumex::store::write_transaction_impl::write_transaction_impl (lumex::id_dispenser::id_t const store_id_a) :
	transaction_impl (store_id_a)
{
}

/*
 * transaction
 */

auto lumex::store::transaction::epoch () const -> epoch_t
{
	return current_epoch;
}

std::chrono::steady_clock::time_point lumex::store::transaction::timestamp () const
{
	return start;
}

/*
 * read_transaction
 */

lumex::store::read_transaction::read_transaction (std::unique_ptr<store::read_transaction_impl> read_transaction_impl) :
	impl (std::move (read_transaction_impl))
{
	start = std::chrono::steady_clock::now ();
}

void * lumex::store::read_transaction::get_handle () const
{
	return impl->get_handle ();
}

lumex::id_dispenser::id_t lumex::store::read_transaction::store_id () const
{
	return impl->store_id;
}

void lumex::store::read_transaction::reset ()
{
	++current_epoch;
	impl->reset ();
}

void lumex::store::read_transaction::renew ()
{
	++current_epoch;
	impl->renew ();
	start = std::chrono::steady_clock::now ();
}

void lumex::store::read_transaction::refresh ()
{
	reset ();
	renew ();
}

bool lumex::store::read_transaction::refresh_if_needed (std::chrono::milliseconds max_age)
{
	auto now = std::chrono::steady_clock::now ();
	if (now - start > max_age)
	{
		refresh ();
		return true;
	}
	return false;
}

/*
 * write_transaction
 */

lumex::store::write_transaction::write_transaction (std::unique_ptr<store::write_transaction_impl> write_transaction_impl) :
	impl (std::move (write_transaction_impl))
{
	/*
	 * For IO threads, we do not want them to block on creating write transactions.
	 */
	debug_assert (lumex::thread_role::get () != lumex::thread_role::name::io);

	start = std::chrono::steady_clock::now ();
}

void * lumex::store::write_transaction::get_handle () const
{
	return impl->get_handle ();
}

lumex::id_dispenser::id_t lumex::store::write_transaction::store_id () const
{
	return impl->store_id;
}

void lumex::store::write_transaction::commit ()
{
	++current_epoch;
	impl->commit ();
}

void lumex::store::write_transaction::renew ()
{
	++current_epoch;
	impl->renew ();
	start = std::chrono::steady_clock::now ();
}

void lumex::store::write_transaction::refresh ()
{
	commit ();
	renew ();
}

void lumex::store::write_transaction::refresh_if_needed (std::chrono::milliseconds max_age)
{
	auto now = std::chrono::steady_clock::now ();
	if (now - start > max_age)
	{
		refresh ();
	}
}

bool lumex::store::write_transaction::contains (lumex::store::table table) const
{
	return impl->contains (table);
}
