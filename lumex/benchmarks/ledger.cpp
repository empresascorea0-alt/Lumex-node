#include <lumex/lib/files.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/node/make_store.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_set_any.hpp>
#include <lumex/store/ledger/account.hpp>

#include <benchmark/benchmark.h>

// Expects live ledger in default location
// PLEASE NOTE: Make sure to purge disk cache between runs (`purge` command on macOS)
static void BM_ledger_iterate_accounts (benchmark::State & state)
{
	lumex::logger logger;
	lumex::stats stats{ logger };

	// Use live ledger
	lumex::network_type network = lumex::network_type::lumex_live_network;
	lumex::network_params network_params{ network };
	auto application_path = lumex::working_path (network);

	auto store_impl{ lumex::make_store (logger, stats, application_path, network_params.ledger, false, true, lumex::node_config{}) };
	auto & store{ *store_impl };

	auto ledger_impl{ std::make_unique<lumex::ledger> (store, network_params, stats, logger, lumex::ledger_options{ .generate_cache = lumex::generate_cache_flags::all_disabled () }) };
	auto & ledger{ *ledger_impl };

	auto transaction = ledger.tx_begin_read ();
	lumex::account current{ 0 };
	lumex::account_info current_info;
	auto it = ledger.any.account_begin (transaction);
	auto end = ledger.any.account_end ();
	for (auto _ : state)
	{
		if (it != end)
		{
			current = it->first;
			current_info = it->second;
			benchmark::DoNotOptimize (current);
			benchmark::DoNotOptimize (current_info);

			++it;
		}
		else
		{
			break;
		}
	}
}
BENCHMARK (BM_ledger_iterate_accounts);

// Expects live ledger in default location
// PLEASE NOTE: Make sure to purge disk cache between runs (`purge` command on macOS)
static void BM_store_iterate_accounts (benchmark::State & state)
{
	lumex::logger logger;
	lumex::stats stats{ logger };

	// Use live ledger
	lumex::network_type network = lumex::network_type::lumex_live_network;
	lumex::network_params network_params{ network };
	lumex::node_flags flags;
	auto application_path = lumex::working_path (network);

	auto store_impl{ lumex::make_store (logger, stats, application_path, network_params.ledger, false, true, lumex::node_config{}) };
	auto & store{ *store_impl };

	auto transaction = store.tx_begin_read ();
	lumex::account current{ 0 };
	lumex::account_info current_info;
	auto it = store.account.begin (transaction);
	auto end = store.account.end (transaction);
	for (auto _ : state)
	{
		if (it != end)
		{
			current = it->first;
			current_info = it->second;
			benchmark::DoNotOptimize (current);
			benchmark::DoNotOptimize (current_info);

			++it;
		}
		else
		{
			break;
		}
	}
}
BENCHMARK (BM_store_iterate_accounts);