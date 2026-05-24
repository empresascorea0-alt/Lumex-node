#include <lumex/crypto_lib/random_pool.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/secure/account_info.hpp>
#include <lumex/secure/pending_info.hpp>
#include <lumex/store/crawler.hpp>
#include <lumex/store/ledger/account.hpp>
#include <lumex/store/ledger/pending.hpp>
#include <lumex/store/ledger_store.hpp>
#include <lumex/test_common/make_store.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <vector>

namespace
{
// Get sequential_attempts threshold from crawler traits
constexpr size_t sequential_threshold = lumex::store::crawler<lumex::store::ledger::account_view, lumex::store::read_transaction>::sequential_attempts;

// Helper to populate accounts with given keys
void populate_accounts (lumex::store::ledger_store & store, std::vector<lumex::uint256_t> const & keys)
{
	auto txn = store.tx_begin_write ();
	for (auto const & key : keys)
	{
		store.account.put (txn, lumex::account{ key }, lumex::account_info{});
	}
}

// Helper to populate pending entries: vector of (account, list of hashes)
void populate_pending (lumex::store::ledger_store & store, std::vector<std::pair<lumex::uint256_t, std::vector<lumex::uint256_t>>> const & entries)
{
	auto txn = store.tx_begin_write ();
	for (auto const & [account, hashes] : entries)
	{
		for (auto const & hash : hashes)
		{
			store.pending.put (txn, lumex::pending_key{ lumex::account{ account }, lumex::block_hash{ hash } }, lumex::pending_info{});
		}
	}
}
}

TEST (crawler, construct_with_data)
{
	auto store = lumex::test::make_store ();

	populate_accounts (*store, { 10, 20, 30 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	ASSERT_TRUE (crawler);
	ASSERT_EQ (crawler.key (), lumex::account{ 10 });
}

TEST (crawler, construct_at_exact_key)
{
	auto store = lumex::test::make_store ();

	populate_accounts (*store, { 10, 20, 30 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn, lumex::account{ 20 });

	ASSERT_TRUE (crawler);
	ASSERT_EQ (crawler.key (), lumex::account{ 20 });
	ASSERT_EQ (crawler->first, lumex::account{ 20 });
}

TEST (crawler, construct_between_keys)
{
	auto store = lumex::test::make_store ();

	populate_accounts (*store, { 10, 30 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn, lumex::account{ 15 });

	// Should position at first key >= 15, which is 30
	ASSERT_TRUE (crawler);
	ASSERT_EQ (crawler.key (), lumex::account{ 30 });
}

TEST (crawler, empty_db)
{
	auto store = lumex::test::make_store ();

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	// Crawler on empty DB should be invalid
	ASSERT_FALSE (crawler);

	// Operations on invalid crawler should be safe
	ASSERT_FALSE (crawler.next ());
	ASSERT_FALSE (crawler.skip_to (lumex::account{ 100 }));
	ASSERT_FALSE (crawler);

	// Seek should keep it invalid (no data to find)
	crawler.seek (lumex::account{ 50 });
	ASSERT_FALSE (crawler);
}

TEST (crawler, dereference_operators)
{
	auto store = lumex::test::make_store ();

	// Create account with specific info
	lumex::account test_account{ 42 };
	lumex::account_info info;
	info.balance = lumex::amount{ 1000 };
	info.block_count = 5;

	{
		auto txn = store->tx_begin_write ();
		store->account.put (txn, test_account, info);
	}

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	ASSERT_TRUE (crawler);

	// Test operator*
	auto const & value = *crawler;
	ASSERT_EQ (value.first, test_account);
	ASSERT_EQ (value.second.balance, lumex::amount{ 1000 });
	ASSERT_EQ (value.second.block_count, 5);

	// Test operator->
	ASSERT_EQ (crawler->first, test_account);
	ASSERT_EQ (crawler->second.balance, lumex::amount{ 1000 });
}

/*
 * Navigation Operations - seek, next, skip_to, reset
 */

TEST (crawler, seek_forward)
{
	auto store = lumex::test::make_store ();

	populate_accounts (*store, { 10, 20, 30, 40, 50 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn, lumex::account{ 10 });

	ASSERT_EQ (crawler.key (), lumex::account{ 10 });

	crawler.seek (lumex::account{ 30 });
	ASSERT_TRUE (crawler);
	ASSERT_EQ (crawler.key (), lumex::account{ 30 });

	// Seek to non-existing key should find next
	crawler.seek (lumex::account{ 35 });
	ASSERT_TRUE (crawler);
	ASSERT_EQ (crawler.key (), lumex::account{ 40 });
}

TEST (crawler, seek_backward)
{
	auto store = lumex::test::make_store ();

	populate_accounts (*store, { 10, 20, 30 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn, lumex::account{ 30 });

	ASSERT_EQ (crawler.key (), lumex::account{ 30 });

	crawler.seek (lumex::account{ 10 });
	ASSERT_TRUE (crawler);
	ASSERT_EQ (crawler.key (), lumex::account{ 10 });
}

TEST (crawler, beyond_range)
{
	auto store = lumex::test::make_store ();

	populate_accounts (*store, { 10, 20 });

	auto txn = store->tx_begin_read ();

	// Construct beyond all keys
	auto crawler1 = store->account.crawl (txn, lumex::account{ std::numeric_limits<lumex::uint256_t>::max () });
	ASSERT_FALSE (crawler1);

	// Seek beyond range
	auto crawler2 = store->account.crawl (txn);
	crawler2.seek (lumex::account{ 100 });
	ASSERT_FALSE (crawler2);

	// skip_to beyond range
	auto crawler3 = store->account.crawl (txn);
	ASSERT_FALSE (crawler3.skip_to (lumex::account{ 100 }));
	ASSERT_FALSE (crawler3);
}

TEST (crawler, operator_increment)
{
	auto store = lumex::test::make_store ();

	populate_accounts (*store, { 10, 20, 30 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	ASSERT_EQ (crawler.key (), lumex::account{ 10 });

	auto & result = ++crawler;
	ASSERT_EQ (&result, &crawler); // Returns reference to self
	ASSERT_EQ (crawler.key (), lumex::account{ 20 });

	++crawler;
	ASSERT_EQ (crawler.key (), lumex::account{ 30 });
}

TEST (crawler, skip_to_exact_match)
{
	auto store = lumex::test::make_store ();

	populate_accounts (*store, { 10, 20, 30 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn, lumex::account{ 10 });

	ASSERT_TRUE (crawler.skip_to (lumex::account{ 20 }));
	ASSERT_EQ (crawler.key (), lumex::account{ 20 });
}

TEST (crawler, skip_to_next_available)
{
	auto store = lumex::test::make_store ();

	populate_accounts (*store, { 10, 30 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn, lumex::account{ 10 });

	ASSERT_TRUE (crawler.skip_to (lumex::account{ 15 }));
	ASSERT_EQ (crawler.key (), lumex::account{ 30 });
}

TEST (crawler, rewind_to_beginning)
{
	auto store = lumex::test::make_store ();

	populate_accounts (*store, { 10, 20, 30 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn, lumex::account{ 30 });

	ASSERT_EQ (crawler.key (), lumex::account{ 30 });

	crawler.rewind ();
	ASSERT_TRUE (crawler);
	ASSERT_EQ (crawler.key (), lumex::account{ 10 });
}

/*
 * Edge Cases
 */

TEST (crawler, single_element)
{
	auto store = lumex::test::make_store ();

	populate_accounts (*store, { 42 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	ASSERT_TRUE (crawler);
	ASSERT_EQ (crawler.key (), lumex::account{ 42 });

	// skip_to same key should stay
	ASSERT_TRUE (crawler.skip_to (lumex::account{ 42 }));
	ASSERT_EQ (crawler.key (), lumex::account{ 42 });

	// next() at end should fail and invalidate crawler
	ASSERT_FALSE (crawler.next ());
	ASSERT_FALSE (crawler);

	// Rewind and test skip_to past end
	crawler.rewind ();
	ASSERT_TRUE (crawler);
	ASSERT_FALSE (crawler.skip_to (lumex::account{ 43 }));
	ASSERT_FALSE (crawler);
}

TEST (crawler, key_saturation_max_value)
{
	auto store = lumex::test::make_store ();

	// Use max value and max-1
	auto max_val = std::numeric_limits<lumex::uint256_t>::max ();
	auto max_minus_one = max_val - 1;

	populate_accounts (*store, { max_minus_one, max_val });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn, lumex::account{ max_minus_one });

	ASSERT_TRUE (crawler);
	ASSERT_EQ (crawler.key ().number (), max_minus_one);

	// Should be able to advance to max
	ASSERT_TRUE (crawler.next ());
	ASSERT_EQ (crawler.key ().number (), max_val);

	// Attempting to go past max should fail due to saturation
	ASSERT_FALSE (crawler.next ());
	ASSERT_FALSE (crawler);
}

/*
 * Simple Keys
 */

TEST (crawler, simple_key_basic_iteration)
{
	auto store = lumex::test::make_store ();

	populate_accounts (*store, { 1, 2, 3, 4, 5 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	std::vector<lumex::uint256_t> visited;
	ASSERT_TRUE (crawler);
	while (crawler)
	{
		visited.push_back (crawler.key ().number ());
		ASSERT_EQ (crawler.key (), crawler.full_key ()); // key() == full_key() for simple keys
		ASSERT_EQ (crawler.key (), lumex::account{ visited.back () }); // key() returns correct value
		bool has_more = crawler.next ();
		ASSERT_EQ (has_more, static_cast<bool> (crawler));
	}

	ASSERT_EQ (visited, (std::vector<lumex::uint256_t>{ 1, 2, 3, 4, 5 }));
}

/*
 * Compound Keys with Grouping
 */

TEST (crawler, compound_key_group_iteration)
{
	auto store = lumex::test::make_store ();

	// Account 1: 3 pending entries
	// Account 2: 2 pending entries
	// Account 3: 1 pending entry
	populate_pending (*store, {
							  { 1, { 10, 20, 30 } },
							  { 2, { 10, 20 } },
							  { 3, { 10 } },
							  });

	auto txn = store->tx_begin_read ();
	auto crawler = store->pending.crawl (txn);

	// First group: account 1
	ASSERT_TRUE (crawler);
	ASSERT_EQ (crawler.key (), lumex::account{ 1 });
	ASSERT_EQ (crawler.full_key ().account, lumex::account{ 1 });
	ASSERT_EQ (crawler.full_key ().hash, lumex::block_hash{ 10 }); // First entry in group

	// next() should skip to account 2, not to next entry in account 1
	ASSERT_TRUE (crawler.next ());
	ASSERT_EQ (crawler.key (), lumex::account{ 2 });

	// next() should skip to account 3
	ASSERT_TRUE (crawler.next ());
	ASSERT_EQ (crawler.key (), lumex::account{ 3 });

	// next() should end iteration
	ASSERT_FALSE (crawler.next ());
	ASSERT_FALSE (crawler);
}

TEST (crawler, compound_key_skip_to)
{
	auto store = lumex::test::make_store ();

	populate_pending (*store, {
							  { 10, { 1, 2, 3 } },
							  { 20, { 1 } },
							  { 30, { 1 } },
							  });

	auto txn = store->tx_begin_read ();
	auto crawler = store->pending.crawl (txn);

	ASSERT_EQ (crawler.key (), lumex::account{ 10 });

	// skip_to same account should stay at current group
	ASSERT_TRUE (crawler.skip_to (lumex::account{ 10 }));
	ASSERT_EQ (crawler.key (), lumex::account{ 10 });

	// skip_to mid-value should land on next group
	ASSERT_TRUE (crawler.skip_to (lumex::account{ 25 }));
	ASSERT_EQ (crawler.key (), lumex::account{ 30 });
}

TEST (crawler, compound_key_many_entries_per_group)
{
	auto store = lumex::test::make_store ();

	// Account 1 with more entries than sequential_threshold to trigger seek fallback
	std::vector<lumex::uint256_t> hashes;
	for (size_t i = 1; i <= sequential_threshold + 1; ++i)
	{
		hashes.push_back (i);
	}
	populate_pending (*store, {
							  { 1, hashes },
							  { 2, { 1 } },
							  });

	auto txn = store->tx_begin_read ();
	auto crawler = store->pending.crawl (txn);

	ASSERT_EQ (crawler.key (), lumex::account{ 1 });

	// next() should skip past all entries to account 2
	// This triggers the seek fallback after sequential attempts exhausted
	ASSERT_TRUE (crawler.next ());
	ASSERT_EQ (crawler.key (), lumex::account{ 2 });

	ASSERT_FALSE (crawler.next ());
}

/*
 * Sequential Optimization - Hybrid Seek Strategy
 */

TEST (crawler, sequential_optimization_within_threshold)
{
	auto store = lumex::test::make_store ();

	// Entries equal to sequential_threshold
	std::vector<lumex::uint256_t> keys;
	for (size_t i = 1; i <= sequential_threshold; ++i)
	{
		keys.push_back (i);
	}
	populate_accounts (*store, keys);

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn, lumex::account{ 1 });

	// skip_to should find target via sequential iteration (within threshold)
	ASSERT_TRUE (crawler.skip_to (lumex::account{ sequential_threshold / 2 }));
	ASSERT_EQ (crawler.key (), lumex::account{ sequential_threshold / 2 });
}

TEST (crawler, sequential_optimization_exceeds_threshold)
{
	auto store = lumex::test::make_store ();

	// More elements than threshold between start and target forces seek fallback
	std::vector<lumex::uint256_t> keys;
	for (size_t i = 1; i <= sequential_threshold + 2; ++i)
	{
		keys.push_back (i);
	}
	keys.push_back (100); // Target far from sequential range
	populate_accounts (*store, keys);

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn, lumex::account{ 1 });

	// skip_to(100) needs to traverse more than threshold elements, triggering seek fallback
	ASSERT_TRUE (crawler.skip_to (lumex::account{ 100 }));
	ASSERT_EQ (crawler.key (), lumex::account{ 100 });
}

/*
 * Ordering Verification
 */

TEST (crawler, iteration_order_ascending)
{
	auto store = lumex::test::make_store ();

	// Insert in non-sorted order
	populate_accounts (*store, { 50, 10, 30, 20, 40 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	std::vector<lumex::uint256_t> order;
	while (crawler)
	{
		order.push_back (crawler.key ().number ());
		++crawler;
	}

	ASSERT_EQ (order, (std::vector<lumex::uint256_t>{ 10, 20, 30, 40, 50 }));
}

/*
 * Large Scale Iteration
 */

TEST (crawler, large_scale_iteration)
{
	auto store = lumex::test::make_store ();

	std::unordered_set<lumex::account> accounts;
	{
		auto txn = store->tx_begin_write ();
		for (int i = 0; i < 1000; ++i)
		{
			lumex::account account;
			lumex::random_pool::generate_block (account.bytes.data (), account.bytes.size ());
			accounts.insert (account);
			store->account.put (txn, account, lumex::account_info{});
		}
	}

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	std::unordered_set<lumex::account> visited;
	lumex::account previous{};
	while (crawler)
	{
		auto current = crawler.key ();
		ASSERT_GT (current.number (), previous.number ());
		visited.insert (current);
		previous = current;
		++crawler;
	}

	ASSERT_EQ (visited.size (), accounts.size ());
	ASSERT_EQ (visited, accounts);
}

TEST (crawler, large_scale_skip_to)
{
	auto store = lumex::test::make_store ();

	std::vector<lumex::account> sorted_accounts;
	{
		auto txn = store->tx_begin_write ();
		for (int i = 0; i < 1000; ++i)
		{
			lumex::account account;
			lumex::random_pool::generate_block (account.bytes.data (), account.bytes.size ());
			sorted_accounts.push_back (account);
			store->account.put (txn, account, lumex::account_info{});
		}
	}
	std::sort (sorted_accounts.begin (), sorted_accounts.end (), [] (auto const & a, auto const & b) {
		return a.number () < b.number ();
	});

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	// Skip to various positions
	for (size_t i : { 100, 250, 500, 750, 999 })
	{
		ASSERT_TRUE (crawler.skip_to (sorted_accounts[i]));
		ASSERT_EQ (crawler.key (), sorted_accounts[i]);
	}
}

TEST (crawler, large_scale_compound_key_groups)
{
	auto store = lumex::test::make_store ();

	// 100 accounts, each with 10 pending entries = 1000 total entries
	std::vector<std::pair<lumex::uint256_t, std::vector<lumex::uint256_t>>> entries;
	for (lumex::uint256_t acc = 1; acc <= 100; ++acc)
	{
		std::vector<lumex::uint256_t> hashes;
		for (lumex::uint256_t h = 1; h <= 10; ++h)
		{
			hashes.push_back (h);
		}
		entries.push_back ({ acc, hashes });
	}
	populate_pending (*store, entries);

	auto txn = store->tx_begin_read ();
	auto crawler = store->pending.crawl (txn);

	size_t group_count = 0;
	lumex::uint256_t previous = 0;
	while (crawler)
	{
		auto current = crawler.key ().number ();
		ASSERT_GT (current, previous);
		previous = current;
		++group_count;
		++crawler;
	}

	// Should have visited exactly 100 groups
	ASSERT_EQ (group_count, 100);
}

/*
 * Skip_to Patterns - Sparse and Dense Data
 */

TEST (crawler, skip_to_sparse_data)
{
	auto store = lumex::test::make_store ();

	// Large gaps between keys
	populate_accounts (*store, { 1, 1000, 1000000 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn, lumex::account{ 1 });

	ASSERT_TRUE (crawler.skip_to (lumex::account{ 500 }));
	ASSERT_EQ (crawler.key (), lumex::account{ 1000 });

	ASSERT_TRUE (crawler.skip_to (lumex::account{ 500000 }));
	ASSERT_EQ (crawler.key (), lumex::account{ 1000000 });

	ASSERT_FALSE (crawler.skip_to (lumex::account{ 2000000 }));
	ASSERT_FALSE (crawler);
}

TEST (crawler, skip_to_dense_data)
{
	auto store = lumex::test::make_store ();

	// Consecutive keys
	populate_accounts (*store, { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn, lumex::account{ 1 });

	ASSERT_TRUE (crawler.skip_to (lumex::account{ 5 }));
	ASSERT_EQ (crawler.key (), lumex::account{ 5 });
}

TEST (crawler, skip_to_backward_target)
{
	auto store = lumex::test::make_store ();

	populate_accounts (*store, { 10, 20, 30, 40, 50 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn, lumex::account{ 30 });

	ASSERT_EQ (crawler.key (), lumex::account{ 30 });

	// skip_to with target less than current position returns true and stays at current
	ASSERT_TRUE (crawler.skip_to (lumex::account{ 10 }));
	ASSERT_EQ (crawler.key (), lumex::account{ 30 });

	// Can still skip forward normally
	ASSERT_TRUE (crawler.skip_to (lumex::account{ 50 }));
	ASSERT_EQ (crawler.key (), lumex::account{ 50 });
}

/*
 * Boundary Conditions
 */

TEST (crawler, boundary_first_key)
{
	auto store = lumex::test::make_store ();

	populate_accounts (*store, { 10, 20, 30 });

	auto txn = store->tx_begin_read ();

	// Construct with start=0 should position at first key
	auto crawler1 = store->account.crawl (txn, lumex::account{ 0 });
	ASSERT_EQ (crawler1.key (), lumex::account{ 10 });

	// Construct with start=first key should position at first key
	auto crawler2 = store->account.crawl (txn, lumex::account{ 10 });
	ASSERT_EQ (crawler2.key (), lumex::account{ 10 });
}

TEST (crawler, boundary_last_key_skip_to_same)
{
	auto store = lumex::test::make_store ();

	populate_accounts (*store, { 10, 20, 30 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn, lumex::account{ 30 });

	// skip_to same key should succeed and stay at same position
	ASSERT_TRUE (crawler.skip_to (lumex::account{ 30 }));
	ASSERT_EQ (crawler.key (), lumex::account{ 30 });

	// skip_to beyond should fail
	ASSERT_FALSE (crawler.skip_to (lumex::account{ 31 }));
	ASSERT_FALSE (crawler);
}

TEST (crawler, boundary_max_is_end)
{
	auto store = lumex::test::make_store ();

	auto max_val = std::numeric_limits<lumex::uint256_t>::max ();

	// Add more than sequential_threshold entries at max account
	// This forces next() to exhaust sequential iteration and hit the saturation check
	{
		auto txn = store->tx_begin_write ();
		for (size_t i = 1; i <= sequential_threshold + 1; ++i)
		{
			store->pending.put (txn,
			lumex::pending_key{ lumex::account{ max_val }, lumex::block_hash{ i } },
			lumex::pending_info{});
		}
	}

	auto txn = store->tx_begin_read ();
	auto crawler = store->pending.crawl (txn, lumex::account{ max_val });

	ASSERT_TRUE (crawler);
	ASSERT_EQ (crawler.key ().number (), max_val);

	// next() exhausts sequential attempts, then tries next_group(max) which saturates to max
	// The saturation check should detect this and move to end
	ASSERT_FALSE (crawler.next ());
	ASSERT_FALSE (crawler);
}

/*
 * Refresh - Transaction refresh with iterator re-establishment
 */

TEST (crawler, refresh)
{
	auto store = lumex::test::make_store ();

	auto txn = store->tx_begin_write ();
	for (lumex::uint256_t i = 1; i <= 10; ++i)
	{
		store->account.put (txn, lumex::account{ i }, lumex::account_info{});
	}

	auto crawler = store->account.crawl (txn);
	ASSERT_EQ (crawler.key (), lumex::account{ 1 });

	// Refresh maintains position
	crawler.refresh ();
	ASSERT_TRUE (crawler);
	ASSERT_EQ (crawler.key (), lumex::account{ 1 });

	// Iterate with periodic refresh (simulates batch processing)
	std::vector<lumex::uint256_t> visited;
	size_t count = 0;
	while (crawler)
	{
		visited.push_back (crawler.key ().number ());
		++crawler;
		++count;
		if (count % 3 == 0)
		{
			crawler.refresh ();
		}
	}
	ASSERT_EQ (visited, (std::vector<lumex::uint256_t>{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 }));

	// Refresh at end stays at end
	ASSERT_FALSE (crawler);
	crawler.refresh ();
	ASSERT_FALSE (crawler);

	// Refresh when current entry was deleted lands on next valid entry
	crawler.rewind ();
	++crawler; // at 2
	++crawler; // at 3
	ASSERT_EQ (crawler.key (), lumex::account{ 3 });
	store->account.del (txn, lumex::account{ 3 });
	crawler.refresh ();
	ASSERT_TRUE (crawler);
	ASSERT_EQ (crawler.key (), lumex::account{ 4 });

	// Refresh when current entry was the last one and deleted → end
	crawler.seek (lumex::account{ 10 });
	ASSERT_EQ (crawler.key (), lumex::account{ 10 });
	store->account.del (txn, lumex::account{ 10 });
	crawler.refresh ();
	ASSERT_FALSE (crawler);
}

TEST (crawler, const_transaction)
{
	auto store = lumex::test::make_store ();

	{
		auto txn = store->tx_begin_write ();
		for (lumex::uint256_t i = 1; i <= 5; ++i)
		{
			store->account.put (txn, lumex::account{ i }, lumex::account_info{});
		}
	}

	auto txn = store->tx_begin_read ();
	auto const & const_txn = txn;
	auto crawler = store->account.crawl (const_txn);

	ASSERT_TRUE (crawler);
	ASSERT_EQ (crawler.key (), lumex::account{ 1 });

	std::vector<lumex::uint256_t> visited;
	while (crawler)
	{
		visited.push_back (crawler.key ().number ());
		++crawler;
	}
	ASSERT_EQ (visited, (std::vector<lumex::uint256_t>{ 1, 2, 3, 4, 5 }));
}
