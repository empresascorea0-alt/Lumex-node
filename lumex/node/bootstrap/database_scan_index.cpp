#include <lumex/lib/utility.hpp>
#include <lumex/node/bootstrap/database_scan_index.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_set_any.hpp>
#include <lumex/store/ledger/account.hpp>
#include <lumex/store/ledger/pending.hpp>

namespace lumex::bootstrap
{
/*
 * database_scan_index
 */

database_scan_index::database_scan_index (lumex::ledger & ledger_a) :
	ledger{ ledger_a },
	account_scanner{ ledger },
	pending_scanner{ ledger }
{
}

void database_scan_index::reset ()
{
	queue.clear ();

	account_scanner.next = lumex::account{ 0 };
	account_scanner.completed = 0;

	pending_scanner.next = lumex::account{ 0 };
	pending_scanner.completed = 0;
}

lumex::account database_scan_index::next (std::function<bool (lumex::account const &)> const & filter)
{
	if (queue.empty ())
	{
		fill ();
	}

	while (!queue.empty ())
	{
		auto result = queue.front ();
		queue.pop_front ();

		if (filter (result))
		{
			return result;
		}
	}

	return { 0 };
}

void database_scan_index::fill ()
{
	auto transaction = ledger.store.tx_begin_read ();

	auto set1 = account_scanner.next_batch (transaction, batch_size);
	auto set2 = pending_scanner.next_batch (transaction, batch_size);

	queue.insert (queue.end (), set1.begin (), set1.end ());
	queue.insert (queue.end (), set2.begin (), set2.end ());
}

bool database_scan_index::warmed_up () const
{
	return account_scanner.completed > 0 && pending_scanner.completed > 0;
}

lumex::container_info database_scan_index::container_info () const
{
	lumex::container_info info;
	info.put ("accounts_iterator", account_scanner.completed);
	info.put ("pending_iterator", pending_scanner.completed);
	return info;
}

/*
 * account_database_scanner
 */

std::deque<lumex::account> account_database_scanner::next_batch (lumex::store::transaction & transaction, size_t batch_size)
{
	std::deque<lumex::account> result;

	auto crawler = ledger.store.account.crawl (transaction, next);

	for (; crawler && result.size () < batch_size; ++crawler)
	{
		auto const & [account, info] = *crawler;
		result.push_back (account);
	}

	// Empty crawler indicates end of ledger
	if (!crawler)
	{
		// Reset for the next ledger iteration
		next = { 0 };
		++completed;
	}
	else
	{
		next = crawler.key ();
	}

	return result;
}

/*
 * pending_database_scanner
 */

std::deque<lumex::account> pending_database_scanner::next_batch (lumex::store::transaction & transaction, size_t batch_size)
{
	std::deque<lumex::account> result;

	auto crawler = ledger.store.pending.crawl (transaction, next);

	for (; crawler && result.size () < batch_size; ++crawler)
	{
		auto const & [key, info] = *crawler;
		result.push_back (key.account);
	}

	// Empty crawler indicates end of ledger
	if (!crawler)
	{
		// Reset for the next ledger iteration
		next = { 0 };
		++completed;
	}
	else
	{
		next = crawler.key ();
	}

	return result;
}
}
