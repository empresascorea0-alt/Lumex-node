#include <lumex/lib/enum_util.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/stats.hpp>

#include <random>

#include <benchmark/benchmark.h>

static void BM_stats_inc_single (benchmark::State & state)
{
	lumex::logger logger;
	lumex::stats stats{ logger };

	for (auto _ : state)
	{
		stats.inc (lumex::stat::type::ledger, lumex::stat::detail::open);
	}
}

BENCHMARK (BM_stats_inc_single);
BENCHMARK (BM_stats_inc_single)->Threads (10);

static void BM_stats_inc_random (benchmark::State & state)
{
	lumex::logger logger;
	lumex::stats stats{ logger };

	auto random_subset = [] (auto elements, size_t count) -> std::vector<typename decltype (elements)::value_type> {
		std::shuffle (elements.begin (), elements.end (), std::mt19937 (std::random_device () ()));
		return { elements.begin (), elements.begin () + std::min (count, elements.size ()) };
	};

	auto stat_types = random_subset (lumex::enum_values<lumex::stat::type> (), state.range (0));
	auto stat_details = random_subset (lumex::enum_values<lumex::stat::detail> (), state.range (1));

	size_t type_index = 0;
	size_t detail_index = 0;

	for (auto _ : state)
	{
		stats.inc (stat_types[type_index], stat_details[detail_index]);

		type_index = (type_index + 1) % stat_types.size ();
		detail_index = (detail_index + 1) % stat_details.size ();
	}
}

BENCHMARK (BM_stats_inc_random)->Args ({ 32, 32 });
BENCHMARK (BM_stats_inc_random)->Args ({ 32, 32 })->Threads (10);