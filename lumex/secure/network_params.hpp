#pragma once

#include <lumex/lib/constants.hpp>
#include <lumex/lib/epochs.hpp>
#include <lumex/lib/fwd.hpp>
#include <lumex/lib/keypair.hpp>
#include <lumex/lib/numbers.hpp>

#include <boost/multiprecision/cpp_int.hpp>

#include <chrono>
#include <memory>

namespace lumex
{
/** Genesis keys and ledger constants for network variants */
class ledger_constants
{
public:
	ledger_constants (lumex::network_type);

	lumex::keypair zero_key;
	lumex::account lumex_beta_account;
	lumex::account lumex_live_account;
	lumex::account lumex_test_account;
	std::shared_ptr<lumex::block> lumex_dev_genesis;
	std::shared_ptr<lumex::block> lumex_beta_genesis;
	std::shared_ptr<lumex::block> lumex_live_genesis;
	std::shared_ptr<lumex::block> lumex_test_genesis;
	std::shared_ptr<lumex::block> genesis;
	lumex::uint128_t genesis_amount;
	lumex::account burn_account;
	lumex::epochs epochs;
};

/** Constants which depend on random values (always used as singleton) */
class hardened_constants
{
public:
	static hardened_constants & get ();

	lumex::account not_an_account;
	lumex::uint128_union random_128;

private:
	hardened_constants ();
};

/** Node related constants whose value depends on the active network */
class node_constants
{
public:
	explicit node_constants (lumex::network_constants const &);

	std::chrono::minutes backup_interval;
	std::chrono::seconds search_pending_interval;
	std::chrono::minutes unchecked_cleaning_interval;

	// Time between collecting online representative samples
	std::chrono::seconds weight_interval;
	// The maximum time to keep online weight samples: 2 weeks on live or 1 day on beta
	std::chrono::seconds weight_cutoff;
};

/** Voting related constants whose value depends on the active network */
class voting_constants
{
public:
	explicit voting_constants (lumex::network_constants const &);

	size_t const max_cache;
	std::chrono::seconds const delay;
};

/** Port-mapping related constants whose value depends on the active network */
class portmapping_constants
{
public:
	explicit portmapping_constants (lumex::network_constants const &);

	// Timeouts are primes so they infrequently happen at the same time
	std::chrono::seconds lease_duration;
	std::chrono::seconds health_check_period;
};

/** Bootstrap related constants whose value depends on the active network */
class bootstrap_constants
{
public:
	explicit bootstrap_constants (lumex::network_constants const &);

	uint32_t lazy_max_pull_blocks;
	uint32_t lazy_min_pull_blocks;
	unsigned frontier_retry_limit;
	unsigned lazy_retry_limit;
	unsigned lazy_destinations_retry_limit;
	std::chrono::milliseconds gap_cache_bootstrap_start_interval;
	uint32_t default_frontiers_age_seconds;
};

lumex::work_thresholds work_thresholds_for_network (lumex::network_type);

/** Constants whose value depends on the active network */
class network_params
{
public:
	explicit network_params (lumex::network_type);

	unsigned kdf_work;
	lumex::work_thresholds work;
	lumex::network_constants network;
	lumex::ledger_constants ledger;
	lumex::voting_constants voting;
	lumex::node_constants node;
	lumex::portmapping_constants portmapping;
	lumex::bootstrap_constants bootstrap;
};

namespace dev
{
	extern lumex::keypair genesis_key;
	extern lumex::network_params network_params;
	extern lumex::ledger_constants & constants;
	extern std::shared_ptr<lumex::block> & genesis;
}
}
