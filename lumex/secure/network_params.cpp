#include <lumex/crypto_lib/random_pool.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/config.hpp>
#include <lumex/lib/env.hpp>
#include <lumex/secure/network_params.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <limits>
#include <sstream>

#include <crypto/ed25519-donna/ed25519.h>

namespace
{
char const * dev_private_key_data = "34F0A37AAD20F4A260F0A5B3CB3D7FB50673212263E58A380BC10474BB039CE4";
char const * dev_public_key_data = "B0311EA55708D6A53C75CDBF88300259C6D018522FE3D4D0A242E431F9E8B6D0"; // xrb_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo
char const * beta_public_key_data = "259A438A8F9F9226130C84D902C237AF3E57C0981C7D709C288046B110D8C8AC"; // lumex_1betagoxpxwykx4kw86dnhosc8t3s7ix8eeentwkcg1hbpez1outjrcyg4n1
char const * live_public_key_data = "2B77EDDB1CF57B1496C25E25BEE2D13A7A74B5A161B5DF440C8F390ABE575611"; // lumex_genesis_account
char const * test_public_key_data = "45C6FF9D1706D61F0821327752671BDA9F9ED2DA40326B01935AB566FB9E08ED"; // lumex_1jg8zygjg3pp5w644emqcbmjqpnzmubfni3kfe1s8pooeuxsw49fdq1mco9j

char const * dev_genesis_data = R"%%%({
	"type": "open",
	"source": "B0311EA55708D6A53C75CDBF88300259C6D018522FE3D4D0A242E431F9E8B6D0",
	"representative": "xrb_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo",
	"account": "xrb_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo",
	"work": "7b42a00ee91d5810",
	"signature": "ECDA914373A2F0CA1296475BAEE40500A7F0A7AD72A5A80C81D7FAB7F6C802B2CC7DB50F5DD0FB25B2EF11761FA7344A158DD5A700B21BD47DE5BD0F63153A02"
    })%%%";

char const * beta_genesis_data = R"%%%({
	"type": "open",
	"source": "259A438A8F9F9226130C84D902C237AF3E57C0981C7D709C288046B110D8C8AC",	
	"representative": "lumex_1betag7az9wk6rbis38s1d35hdsycz1bi95xg4g4j148p6afjk7embcurda4",
	"account": "lumex_1betag7az9wk6rbis38s1d35hdsycz1bi95xg4g4j148p6afjk7embcurda4",	
	"work": "e87a3ce39b43b84c",
	"signature": "BC588273AC689726D129D3137653FB319B6EE6DB178F97421D11D075B46FD52B6748223C8FF4179399D35CB1A8DF36F759325BD2D3D4504904321FAFB71D7602"
	})%%%";

char const * live_genesis_data = R"%%%({
	"type": "open",
	"source": "2B77EDDB1CF57B1496C25E25BEE2D13A7A74B5A161B5DF440C8F390ABE575611",
	"representative": "2B77EDDB1CF57B1496C25E25BEE2D13A7A74B5A161B5DF440C8F390ABE575611",
	"account": "2B77EDDB1CF57B1496C25E25BEE2D13A7A74B5A161B5DF440C8F390ABE575611",
	"work": "0000000000000000",
	"signature": "BF33DE7C9C4E10BD563EE7C46661616877CCA6CBC82AA9B0CBD669C27EF5E734DB08514024EE3C171A1A9DBAFAF85829F5601B5E48DE795F9F40CB8333E16200"
    })%%%";

std::string const test_genesis_data = lumex::env::get ("LUMEX_TEST_GENESIS_BLOCK").value_or (R"%%%({
	"type": "open",
	"source": "45C6FF9D1706D61F0821327752671BDA9F9ED2DA40326B01935AB566FB9E08ED",
	"representative": "lumex_1jg8zygjg3pp5w644emqcbmjqpnzmubfni3kfe1s8pooeuxsw49fdq1mco9j",
	"account": "lumex_1jg8zygjg3pp5w644emqcbmjqpnzmubfni3kfe1s8pooeuxsw49fdq1mco9j",
	"work": "bc1ef279c1a34eb1",
	"signature": "15049467CAEE3EC768639E8E35792399B6078DA763DA4EBA8ECAD33B0EDC4AF2E7403893A5A602EB89B978DABEF1D6606BB00F3C0EE11449232B143B6E07170E"
    })%%%");

std::shared_ptr<lumex::block> parse_block_from_genesis_data (std::string const & genesis_data_a)
{
	boost::property_tree::ptree tree;
	std::stringstream istream (genesis_data_a);
	boost::property_tree::read_json (istream, tree);
	return lumex::deserialize_block_json (tree);
}
}

/*
 * lumex::dev constants
 */

lumex::keypair lumex::dev::genesis_key{ dev_private_key_data };
lumex::network_params lumex::dev::network_params{ lumex::network_type::lumex_dev_network };
lumex::ledger_constants & lumex::dev::constants{ lumex::dev::network_params.ledger };
std::shared_ptr<lumex::block> & lumex::dev::genesis = lumex::dev::constants.genesis;

/*
 *
 */

lumex::work_thresholds lumex::work_thresholds_for_network (lumex::network_type network_type)
{
	switch (network_type)
	{
		case lumex::network_type::lumex_live_network:
			return lumex::work_thresholds::publish_full;
		case lumex::network_type::lumex_beta_network:
			return lumex::work_thresholds::publish_beta;
		case lumex::network_type::lumex_dev_network:
			return lumex::work_thresholds::publish_dev;
		case lumex::network_type::lumex_test_network:
			return lumex::work_thresholds::publish_test;
		default:
			release_assert (false, "invalid network");
	}
}

lumex::network_params::network_params (lumex::network_type network_type) :
	work{ work_thresholds_for_network (network_type) },
	network{ work, network_type },
	ledger{ network_type },
	voting{ network },
	node{ network },
	portmapping{ network },
	bootstrap{ network }
{
	unsigned constexpr kdf_full_work = 64 * 1024;
	unsigned constexpr kdf_dev_work = 8;
	kdf_work = network.is_dev_network () ? kdf_dev_work : kdf_full_work;
}

/*
 *
 */

lumex::ledger_constants::ledger_constants (lumex::network_type network_type) :
	zero_key{ "0" },
	lumex_beta_account{ beta_public_key_data },
	lumex_live_account{ live_public_key_data },
	lumex_test_account{ test_public_key_data },
	lumex_dev_genesis{ parse_block_from_genesis_data (dev_genesis_data) },
	lumex_beta_genesis{ parse_block_from_genesis_data (beta_genesis_data) },
	lumex_live_genesis{ parse_block_from_genesis_data (live_genesis_data) },
	lumex_test_genesis{ parse_block_from_genesis_data (test_genesis_data) },
	genesis_amount{ lumex::uint128_t ("20000000000000000000000000000000000000") },
	burn_account{ lumex::account{ 0 } }
{
	lumex_beta_genesis->sideband_set (lumex::block_sideband{
	/* account */ lumex_beta_genesis->account_field ().value (),
	/* balance (amount) */ lumex::amount{ lumex::uint128_t ("20000000000000000000000000000000000000") },
	/* height */ uint64_t{ 1 },
	/* local_timestamp */ 0,
	/* epoch */ lumex::epoch::epoch_0,
	/* is_send */ false,
	/* is_receive */ false,
	/* is_epoch */ false,
	/* source_epoch */ lumex::epoch::epoch_0,
	/* topo_height */ 1 });

	lumex_dev_genesis->sideband_set (lumex::block_sideband{
	/* account */ lumex_dev_genesis->account_field ().value (),
	/* balance (amount) */ lumex::amount{ lumex::uint128_t ("20000000000000000000000000000000000000") },
	/* height */ uint64_t{ 1 },
	/* local_timestamp */ 0,
	/* epoch */ lumex::epoch::epoch_0,
	/* is_send */ false,
	/* is_receive */ false,
	/* is_epoch */ false,
	/* source_epoch */ lumex::epoch::epoch_0,
	/* topo_height */ 1 });

	lumex_live_genesis->sideband_set (lumex::block_sideband{
	/* account */ lumex_live_genesis->account_field ().value (),
	/* balance (amount) */ lumex::amount{ lumex::uint128_t ("20000000000000000000000000000000000000") },
	/* height */ uint64_t{ 1 },
	/* local_timestamp */ 0,
	/* epoch */ lumex::epoch::epoch_0,
	/* is_send */ false,
	/* is_receive */ false,
	/* is_epoch */ false,
	/* source_epoch */ lumex::epoch::epoch_0,
	/* topo_height */ 1 });

	lumex_test_genesis->sideband_set (lumex::block_sideband{
	/* account */ lumex_test_genesis->account_field ().value (),
	/* balance (amount) */ lumex::amount{ lumex::uint128_t ("20000000000000000000000000000000000000") },
	/* height */ uint64_t{ 1 },
	/* local_timestamp */ 0,
	/* epoch */ lumex::epoch::epoch_0,
	/* is_send */ false,
	/* is_receive */ false,
	/* is_epoch */ false,
	/* source_epoch */ lumex::epoch::epoch_0,
	/* topo_height */ 1 });

	lumex::account epoch_v2_signer;
	switch (network_type)
	{
		case lumex::network_type::lumex_dev_network:
		{
			genesis = lumex_dev_genesis;
			epoch_v2_signer = lumex::dev::genesis_key.pub;
		}
		break;
		case lumex::network_type::lumex_live_network:
		{
			genesis = lumex_live_genesis;
			epoch_v2_signer = lumex::account::from_account ("lumex_3qb6o6i1tkzr6jwr5s7eehfxwg9x6eemitdinbpi7u8bjjwsgqfj4wzser3x");
		}
		break;
		case lumex::network_type::lumex_beta_network:
		{
			genesis = lumex_beta_genesis;
			epoch_v2_signer = lumex_beta_account;
		}
		break;
		case lumex::network_type::lumex_test_network:
		{
			genesis = lumex_test_genesis;
			epoch_v2_signer = lumex_test_account;
		}
		break;
		default:
			release_assert (false, "invalid network");
			break;
	}
	release_assert (genesis != nullptr);
	release_assert (!epoch_v2_signer.is_zero ());

	lumex::link const epoch_link_v1{ "epoch v1 block" };
	epochs.add (lumex::epoch::epoch_1, genesis->account (), epoch_link_v1);

	lumex::link const epoch_link_v2{ "epoch v2 block" };
	epochs.add (lumex::epoch::epoch_2, epoch_v2_signer, epoch_link_v2);
}

/*
 *
 */

lumex::hardened_constants & lumex::hardened_constants::get ()
{
	static hardened_constants instance{};
	return instance;
}

lumex::hardened_constants::hardened_constants () :
	not_an_account{},
	random_128{}
{
	lumex::random_pool::generate_block (not_an_account.bytes.data (), not_an_account.bytes.size ());
	lumex::random_pool::generate_block (random_128.bytes.data (), random_128.bytes.size ());
}

/*
 *
 */

lumex::node_constants::node_constants (lumex::network_constants const & network_constants)
{
	backup_interval = std::chrono::minutes (5);
	search_pending_interval = network_constants.is_dev_network () ? std::chrono::seconds (1) : std::chrono::seconds (5 * 60);
	unchecked_cleaning_interval = std::chrono::minutes (30);
	weight_interval = network_constants.is_dev_network () ? std::chrono::seconds (1) : std::chrono::minutes (5);
	weight_cutoff = (network_constants.is_live_network () || network_constants.is_test_network ()) ? std::chrono::weeks (2) : std::chrono::days (1);
}

/*
 *
 */

lumex::voting_constants::voting_constants (lumex::network_constants const & network_constants) :
	max_cache{ network_constants.is_dev_network () ? 256U : 128U * 1024 },
	delay{ network_constants.is_dev_network () ? 1 : 15 }
{
}

/*
 *
 */

lumex::portmapping_constants::portmapping_constants (lumex::network_constants const & network_constants)
{
	lease_duration = std::chrono::seconds (1787); // ~30 minutes
	health_check_period = std::chrono::seconds (53);
}

/*
 *
 */

lumex::bootstrap_constants::bootstrap_constants (lumex::network_constants const & network_constants)
{
	lazy_max_pull_blocks = network_constants.is_dev_network () ? 2 : 512;
	lazy_min_pull_blocks = network_constants.is_dev_network () ? 1 : 32;
	frontier_retry_limit = network_constants.is_dev_network () ? 2 : 16;
	lazy_retry_limit = network_constants.is_dev_network () ? 2 : frontier_retry_limit * 4;
	lazy_destinations_retry_limit = network_constants.is_dev_network () ? 1 : frontier_retry_limit / 4;
	gap_cache_bootstrap_start_interval = network_constants.is_dev_network () ? std::chrono::milliseconds (5) : std::chrono::milliseconds (30 * 1000);
	default_frontiers_age_seconds = network_constants.is_dev_network () ? 1 : 24 * 60 * 60; // 1 second for dev network, 24 hours for live/beta
}
