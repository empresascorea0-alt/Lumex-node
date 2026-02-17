#include <nano/lib/bounded_dfs.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <vector>

namespace
{
// Simple graph representation for testing: node -> list of dependencies
using graph_t = std::map<int, std::vector<int>>;

// Helper that runs bounded_dfs on an int graph, tracking resolve order
nano::bounded_dfs_result run_dfs (graph_t const & graph, int start, size_t max_depth, std::vector<int> & resolve_order)
{
	std::set<int> resolved;
	return nano::bounded_dfs (
	start, max_depth,
	[&] (int const & node) {
		return resolved.contains (node);
	},
	[&] (int const & node) -> std::vector<int> {
		auto it = graph.find (node);
		if (it != graph.end ())
		{
			return it->second;
		}
		return {};
	},
	[&] (int const & node) {
		resolved.insert (node);
		resolve_order.push_back (node);
		return true;
	});
}
}

TEST (bounded_dfs, single_node_no_deps)
{
	graph_t graph = { { 1, {} } };
	std::vector<int> order;
	auto result = run_dfs (graph, 1, 100, order);
	ASSERT_EQ (result.resolved, 1);
	ASSERT_FALSE (result.overflow);
	ASSERT_EQ (order, std::vector<int> ({ 1 }));
}

TEST (bounded_dfs, linear_chain)
{
	// 4 -> 3 -> 2 -> 1 (each depends on the next)
	graph_t graph = {
		{ 1, {} },
		{ 2, { 1 } },
		{ 3, { 2 } },
		{ 4, { 3 } },
	};
	std::vector<int> order;
	auto result = run_dfs (graph, 4, 100, order);
	ASSERT_EQ (result.resolved, 4);
	ASSERT_FALSE (result.overflow);
	// Must resolve in dependency order: 1, 2, 3, 4
	ASSERT_EQ (order, std::vector<int> ({ 1, 2, 3, 4 }));
}

TEST (bounded_dfs, diamond)
{
	//     4
	//    / \
	//   2   3
	//    \ /
	//     1
	graph_t graph = {
		{ 1, {} },
		{ 2, { 1 } },
		{ 3, { 1 } },
		{ 4, { 2, 3 } },
	};
	std::vector<int> order;
	auto result = run_dfs (graph, 4, 100, order);
	ASSERT_EQ (result.resolved, 4);
	ASSERT_FALSE (result.overflow);
	// 1 must be first, 4 must be last
	ASSERT_EQ (order.front (), 1);
	ASSERT_EQ (order.back (), 4);
	// 2 and 3 must come after 1 and before 4
	auto pos = [&] (int v) { return std::find (order.begin (), order.end (), v) - order.begin (); };
	ASSERT_GT (pos (2), pos (1));
	ASSERT_GT (pos (3), pos (1));
	ASSERT_LT (pos (2), pos (4));
	ASSERT_LT (pos (3), pos (4));
}

TEST (bounded_dfs, already_resolved)
{
	graph_t graph = { { 1, {} } };
	std::set<int> pre_resolved = { 1 };
	std::vector<int> order;
	auto result = nano::bounded_dfs (
	1, 100,
	[&] (int const & node) { return pre_resolved.contains (node); },
	[&] (int const & node) -> std::vector<int> { return graph.at (node); },
	[&] (int const & node) { order.push_back (node); return true; });
	ASSERT_EQ (result.resolved, 0);
	ASSERT_FALSE (result.overflow);
	ASSERT_TRUE (order.empty ());
}

TEST (bounded_dfs, partially_resolved)
{
	// 3 -> 2 -> 1, but 1 and 2 are already resolved
	graph_t graph = {
		{ 1, {} },
		{ 2, { 1 } },
		{ 3, { 2 } },
	};
	std::set<int> pre_resolved = { 1, 2 };
	std::vector<int> order;
	auto result = nano::bounded_dfs (
	3, 100,
	[&] (int const & node) { return pre_resolved.contains (node); },
	[&] (int const & node) -> std::vector<int> { return graph.at (node); },
	[&] (int const & node) {
		pre_resolved.insert (node);
		order.push_back (node);
		return true;
	});
	ASSERT_EQ (result.resolved, 1);
	ASSERT_FALSE (result.overflow);
	ASSERT_EQ (order, std::vector<int> ({ 3 }));
}

TEST (bounded_dfs, overflow_deep_chain)
{
	// Chain: 5 -> 4 -> 3 -> 2 -> 1, max_depth = 3
	graph_t graph = {
		{ 1, {} },
		{ 2, { 1 } },
		{ 3, { 2 } },
		{ 4, { 3 } },
		{ 5, { 4 } },
	};
	std::vector<int> order;
	auto result = run_dfs (graph, 5, 3, order);
	ASSERT_TRUE (result.overflow);
	// Some progress should have been made (resolved nodes at bottom of chain)
	ASSERT_EQ (result.resolved, 3);
	ASSERT_EQ (order, std::vector<int> ({ 1, 2, 3 }));
}

TEST (bounded_dfs, overflow_multi_pass)
{
	// Chain: 5 -> 4 -> 3 -> 2 -> 1, max_depth = 3
	// Run multiple passes until fully resolved
	graph_t graph = {
		{ 1, {} },
		{ 2, { 1 } },
		{ 3, { 2 } },
		{ 4, { 3 } },
		{ 5, { 4 } },
	};
	std::set<int> resolved;
	std::vector<int> order;
	size_t total_resolved = 0;
	int passes = 0;
	while (true)
	{
		auto result = nano::bounded_dfs (
		5, 3,
		[&] (int const & node) { return resolved.contains (node); },
		[&] (int const & node) -> std::vector<int> { return graph.at (node); },
		[&] (int const & node) {
			resolved.insert (node);
			order.push_back (node);
			return true;
		});
		total_resolved += result.resolved;
		++passes;
		if (!result.overflow)
		{
			break;
		}
		// Safety: prevent infinite loop in test
		ASSERT_LT (passes, 10);
	}
	ASSERT_EQ (total_resolved, 5);
	ASSERT_EQ (order, std::vector<int> ({ 1, 2, 3, 4, 5 }));
	ASSERT_EQ (passes, 2);
}

TEST (bounded_dfs, wide_fan_out)
{
	// Node 10 depends on 1..5, each with no deps
	graph_t graph = {
		{ 1, {} },
		{ 2, {} },
		{ 3, {} },
		{ 4, {} },
		{ 5, {} },
		{ 10, { 1, 2, 3, 4, 5 } },
	};
	std::vector<int> order;
	auto result = run_dfs (graph, 10, 100, order);
	ASSERT_EQ (result.resolved, 6);
	ASSERT_FALSE (result.overflow);
	// 10 must be last
	ASSERT_EQ (order.back (), 10);
	// All deps must come before 10
	for (int i = 1; i <= 5; ++i)
	{
		ASSERT_NE (std::find (order.begin (), order.end (), i), order.end ());
	}
}

TEST (bounded_dfs, zero_deps_skipped)
{
	// Use 0 as "null" dependency that is_resolved returns true for
	graph_t graph = {
		{ 1, { 0, 0 } }, // both deps are zero/null
	};
	std::vector<int> order;
	auto result = nano::bounded_dfs (
	1, 100,
	[&] (int const & node) { return node == 0; },
	[&] (int const & node) -> std::vector<int> { return graph.at (node); },
	[&] (int const & node) { order.push_back (node); return true; });
	ASSERT_EQ (result.resolved, 1);
	ASSERT_FALSE (result.overflow);
	ASSERT_EQ (order, std::vector<int> ({ 1 }));
}

TEST (bounded_dfs, max_depth_one)
{
	// With max_depth=1, drops start to push dep, resolves only the leaf
	graph_t graph = {
		{ 1, {} },
		{ 2, { 1 } },
	};
	std::vector<int> order;
	auto result = run_dfs (graph, 2, 1, order);
	ASSERT_TRUE (result.overflow);
	ASSERT_EQ (result.resolved, 1);
	ASSERT_EQ (order, std::vector<int> ({ 1 }));
}

TEST (bounded_dfs, complex_dag)
{
	//       7
	//      / \
	//     5   6
	//    / \ / \
	//   2   3   4
	//    \ /
	//     1
	graph_t graph = {
		{ 1, {} },
		{ 2, { 1 } },
		{ 3, { 1 } },
		{ 4, {} },
		{ 5, { 2, 3 } },
		{ 6, { 3, 4 } },
		{ 7, { 5, 6 } },
	};
	std::vector<int> order;
	auto result = run_dfs (graph, 7, 100, order);
	ASSERT_EQ (result.resolved, 7);
	ASSERT_FALSE (result.overflow);
	// Verify topological ordering: every node appears after all its deps
	auto pos = [&] (int v) { return std::find (order.begin (), order.end (), v) - order.begin (); };
	for (auto const & [node, deps] : graph)
	{
		for (auto dep : deps)
		{
			ASSERT_LT (pos (dep), pos (node)) << "node " << node << " should appear after dep " << dep;
		}
	}
}

TEST (bounded_dfs, array_deps)
{
	// Test with std::array return type (like nano::block::dependencies())
	std::set<int> resolved;
	std::vector<int> order;
	auto result = nano::bounded_dfs (
	3, 100,
	[&] (int const & node) { return node == 0 || resolved.contains (node); },
	[&] (int const & node) -> std::array<int, 2> {
		switch (node)
		{
			case 1:
				return { 0, 0 }; // no deps
			case 2:
				return { 1, 0 }; // depends on 1
			case 3:
				return { 2, 1 }; // depends on 2 and 1
			default:
				return { 0, 0 };
		}
	},
	[&] (int const & node) {
		resolved.insert (node);
		order.push_back (node);
		return true;
	});
	ASSERT_EQ (result.resolved, 3);
	ASSERT_FALSE (result.overflow);
	ASSERT_EQ (order, std::vector<int> ({ 1, 2, 3 }));
}

TEST (bounded_dfs, no_duplicate_resolves)
{
	// Shared dependency resolved only once despite multiple parents
	graph_t graph = {
		{ 1, {} },
		{ 2, { 1 } },
		{ 3, { 1 } },
		{ 4, { 2, 3 } },
	};
	std::vector<int> order;
	auto result = run_dfs (graph, 4, 100, order);
	// Node 1 appears exactly once despite being dep of both 2 and 3
	ASSERT_EQ (std::count (order.begin (), order.end (), 1), 1);
	ASSERT_EQ (result.resolved, 4);
}

TEST (bounded_dfs, early_return)
{
	// Chain: 4 -> 3 -> 2 -> 1, resolve returns false after 2 nodes
	graph_t graph = {
		{ 1, {} },
		{ 2, { 1 } },
		{ 3, { 2 } },
		{ 4, { 3 } },
	};
	std::set<int> resolved;
	std::vector<int> order;
	auto result = nano::bounded_dfs (
	4, 100,
	[&] (int const & node) { return resolved.contains (node); },
	[&] (int const & node) -> std::vector<int> { return graph.at (node); },
	[&] (int const & node) {
		resolved.insert (node);
		order.push_back (node);
		return order.size () < 2; // Stop after resolving 2 nodes
	});
	ASSERT_EQ (result.resolved, 2);
	ASSERT_TRUE (result.overflow); // Early return signals overflow so caller knows to retry
	ASSERT_EQ (order, std::vector<int> ({ 1, 2 }));
}

TEST (bounded_dfs, early_return_multi_pass)
{
	// Chain: 6 -> 5 -> 4 -> 3 -> 2 -> 1, resolve returns false after 2 nodes per pass
	// Verify that early return overflow enables multi-pass completion
	graph_t graph = {
		{ 1, {} },
		{ 2, { 1 } },
		{ 3, { 2 } },
		{ 4, { 3 } },
		{ 5, { 4 } },
		{ 6, { 5 } },
	};
	std::set<int> resolved;
	std::vector<int> order;
	size_t total_resolved = 0;
	int passes = 0;
	while (true)
	{
		size_t pass_start = order.size ();
		auto result = nano::bounded_dfs (
		6, 100,
		[&] (int const & node) { return resolved.contains (node); },
		[&] (int const & node) -> std::vector<int> { return graph.at (node); },
		[&] (int const & node) {
			resolved.insert (node);
			order.push_back (node);
			return (order.size () - pass_start) < 2; // Max 2 resolves per pass
		});
		total_resolved += result.resolved;
		++passes;
		if (!result.overflow)
		{
			break;
		}
		ASSERT_LT (passes, 10);
	}
	ASSERT_EQ (total_resolved, 6);
	ASSERT_EQ (order, std::vector<int> ({ 1, 2, 3, 4, 5, 6 }));
	// 3 productive passes (2 resolves each) + 1 final pass that finds everything resolved
	ASSERT_EQ (passes, 4);
}

TEST (bounded_dfs, push_all_deps)
{
	// Verify all unresolved deps are pushed at once (LIFO: last dep processed first)
	//   3
	//  / \
	// 1   2  (deps listed as {1, 2})
	graph_t graph = {
		{ 1, {} },
		{ 2, {} },
		{ 3, { 1, 2 } },
	};
	std::vector<int> order;
	auto result = run_dfs (graph, 3, 100, order);
	ASSERT_EQ (result.resolved, 3);
	ASSERT_FALSE (result.overflow);
	// Push-all with LIFO means 2 (pushed last) is processed first
	ASSERT_EQ (order, std::vector<int> ({ 2, 1, 3 }));
}

TEST (bounded_dfs, shared_dep_not_loaded_twice)
{
	// Diamond DAG where shared dep is pushed by both parents (push-all)
	// Verify get_dependencies is only called once for the shared dep
	//     4
	//    / \
	//   2   3
	//    \ /
	//     1
	graph_t graph = {
		{ 1, {} },
		{ 2, { 1 } },
		{ 3, { 1 } },
		{ 4, { 2, 3 } },
	};
	std::set<int> resolved;
	std::map<int, int> dep_call_count;
	std::vector<int> order;
	nano::bounded_dfs (
	4, 100,
	[&] (int const & node) { return resolved.contains (node); },
	[&] (int const & node) -> std::vector<int> {
		dep_call_count[node]++;
		return graph.at (node);
	},
	[&] (int const & node) {
		resolved.insert (node);
		order.push_back (node);
		return true;
	});
	// With LIFO, node 3 is processed first and resolves node 1.
	// When node 2 is visited, node 1 is already resolved and not pushed again.
	ASSERT_EQ (dep_call_count[1], 1);
	ASSERT_EQ (std::count (order.begin (), order.end (), 1), 1);
}

TEST (bounded_dfs, already_resolved_no_resolve)
{
	// Start node already resolved: get_dependencies is called but resolve is not
	graph_t graph = { { 1, { 2 } }, { 2, {} } };
	std::set<int> pre_resolved = { 1, 2 };
	int dep_calls = 0;
	int resolve_calls = 0;
	auto result = nano::bounded_dfs (
	1, 100,
	[&] (int const & node) { return pre_resolved.contains (node); },
	[&] (int const & node) -> std::vector<int> {
		dep_calls++;
		return graph.at (node);
	},
	[&] (int const & node) {
		resolve_calls++;
		return true;
	});
	ASSERT_EQ (result.resolved, 0);
	ASSERT_FALSE (result.overflow);
	ASSERT_EQ (dep_calls, 1); // Deps loaded for start node
	ASSERT_EQ (resolve_calls, 0); // But resolve never called
}

TEST (bounded_dfs, callback_counts)
{
	// Complex DAG that exercises push-all, shared deps, and dep rescanning
	// Exact callback counts serve as a regression guard for algorithm efficiency
	//       7
	//      / \
	//     5   6
	//    / \ / \
	//   2   3   4
	//    \ /
	//     1
	graph_t graph = {
		{ 1, {} },
		{ 2, { 1 } },
		{ 3, { 1 } },
		{ 4, {} },
		{ 5, { 2, 3 } },
		{ 6, { 3, 4 } },
		{ 7, { 5, 6 } },
	};
	std::set<int> resolved;
	int is_resolved_calls = 0;
	int get_deps_calls = 0;
	int resolve_calls = 0;
	std::vector<int> order;
	auto result = nano::bounded_dfs (
	7, 100,
	[&] (int const & node) {
		++is_resolved_calls;
		return resolved.contains (node);
	},
	[&] (int const & node) -> std::vector<int> {
		++get_deps_calls;
		return graph.at (node);
	},
	[&] (int const & node) {
		++resolve_calls;
		resolved.insert (node);
		order.push_back (node);
		return true;
	});
	ASSERT_EQ (result.resolved, 7);
	ASSERT_FALSE (result.overflow);
	ASSERT_EQ (get_deps_calls, 7); // Once per node
	ASSERT_EQ (resolve_calls, 7); // Once per node
	ASSERT_EQ (is_resolved_calls, 22);
}
