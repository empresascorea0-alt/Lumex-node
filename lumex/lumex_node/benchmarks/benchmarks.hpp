#pragma once

#include <lumex/lib/numbers_templ.hpp>
#include <lumex/node/node.hpp>
#include <lumex/secure/common.hpp>

#include <boost/program_options.hpp>

#include <atomic>
#include <memory>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lumex::cli
{
enum class cementing_mode
{
	sequential,
	root
};

class account_pool
{
private:
	std::vector<lumex::keypair> keys;
	std::unordered_map<lumex::account, lumex::keypair> account_to_keypair;
	std::unordered_map<lumex::account, lumex::uint128_t> balances;
	std::vector<lumex::account> accounts_with_balance;
	std::unordered_set<lumex::account> balance_lookup;
	std::unordered_map<lumex::account, lumex::block_hash> frontiers;
	std::random_device rd;
	std::mt19937 gen;

public:
	account_pool ();

	void generate_accounts (size_t count);
	lumex::account get_random_account_with_balance ();
	lumex::account get_random_account ();
	lumex::keypair const & get_keypair (lumex::account const & account);
	void update_balance (lumex::account const & account, lumex::uint128_t new_balance);
	lumex::uint128_t get_balance (lumex::account const & account);
	bool has_balance (lumex::account const & account);
	size_t accounts_with_balance_count () const;
	size_t total_accounts () const;
	std::vector<lumex::account> get_accounts_with_balance () const;
	void set_initial_balance (lumex::account const & account, lumex::uint128_t balance);
	void set_frontier (lumex::account const & account, lumex::block_hash const & frontier);
	lumex::block_hash get_frontier (lumex::account const & account) const;
};

struct benchmark_config
{
	size_t num_accounts{ 150000 };
	size_t num_iterations{ 5 };
	size_t batch_size{ 250000 };
	lumex::cli::cementing_mode cementing_mode{ lumex::cli::cementing_mode::sequential };

	static benchmark_config parse (boost::program_options::variables_map const & vm);
};

class benchmark_base
{
protected:
	account_pool pool;
	std::shared_ptr<lumex::node> node;
	benchmark_config config;

	// Common metrics
	std::atomic<size_t> processed_blocks_count{ 0 };

public:
	benchmark_base (std::shared_ptr<lumex::node> node_a, benchmark_config const & config_a);
	virtual ~benchmark_base () = default;

	// Transfers genesis balance to a random account to prepare for benchmarking
	void setup_genesis_distribution (double distribution_percentage = 1.0);

	// Generates random transfer pairs between accounts with no specific dependency structure
	std::deque<std::shared_ptr<lumex::block>> generate_random_transfers ();

	// Generates blocks that are dependencies of a single root block (last in deque)
	std::deque<std::shared_ptr<lumex::block>> generate_dependent_chain ();

	// Generates independent blocks - returns sends and opens separately
	std::pair<std::deque<std::shared_ptr<lumex::block>>, std::deque<std::shared_ptr<lumex::block>>> generate_independent_blocks ();
};

// Benchmark entry points - individual implementations are in separate cpp files
void run_block_processing_benchmark (boost::program_options::variables_map const & vm, std::filesystem::path const & data_path);
void run_cementing_benchmark (boost::program_options::variables_map const & vm, std::filesystem::path const & data_path);
void run_elections_benchmark (boost::program_options::variables_map const & vm, std::filesystem::path const & data_path);
void run_pipeline_benchmark (boost::program_options::variables_map const & vm, std::filesystem::path const & data_path);
}