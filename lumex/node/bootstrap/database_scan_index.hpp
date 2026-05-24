#pragma once

#include <lumex/lib/container_info.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/secure/pending_info.hpp>

#include <deque>

namespace lumex::bootstrap
{
struct account_database_scanner
{
	lumex::ledger & ledger;

	std::deque<lumex::account> next_batch (lumex::store::transaction &, size_t batch_size);

	lumex::account next{ 0 };
	size_t completed{ 0 };
};

struct pending_database_scanner
{
	lumex::ledger & ledger;

	std::deque<lumex::account> next_batch (lumex::store::transaction &, size_t batch_size);

	lumex::account next{ 0 };
	size_t completed{ 0 };
};

class database_scan_index
{
public:
	explicit database_scan_index (lumex::ledger &);

	lumex::account next (std::function<bool (lumex::account const &)> const & filter);

	// Indicates if a full ledger iteration has taken place e.g. warmed up
	bool warmed_up () const;

	void reset ();

	lumex::container_info container_info () const;

private: // Dependencies
	lumex::ledger & ledger;

private:
	void fill ();

private:
	account_database_scanner account_scanner;
	pending_database_scanner pending_scanner;

	std::deque<lumex::account> queue;

	static size_t constexpr batch_size = 512;
};
}
