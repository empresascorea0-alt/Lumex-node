#pragma once

#include <lumex/lib/blocks.hpp>
#include <lumex/lib/errors.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/threading.hpp>
#include <lumex/lib/work.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/transport/channel.hpp>
#include <lumex/secure/network_params.hpp>

#include <chrono>
#include <optional>

namespace lumex
{
/** Test-system related error codes */
enum class error_system
{
	generic = 1,
	deadline_expired
};

namespace test
{
	class system final
	{
	public:
		system ();
		system (uint16_t, lumex::transport::transport_type = lumex::transport::transport_type::tcp);
		system (uint16_t, lumex::transport::transport_type, lumex::node_flags const &);
		~system ();

		void stop ();

		void set_initialization_blocks (std::deque<std::shared_ptr<lumex::block>> blocks);
		void set_cemented_initialization_blocks (std::deque<std::shared_ptr<lumex::block>> blocks);

		void ledger_initialization_set (std::deque<lumex::keypair> const & reps, lumex::amount const & reserve = 0);
		void generate_activity (lumex::node &, std::vector<lumex::account> &);
		void generate_mass_activity (uint32_t, lumex::node &);
		void generate_usage_traffic (uint32_t, uint32_t, size_t);
		void generate_usage_traffic (uint32_t, uint32_t);
		lumex::account get_random_account (std::vector<lumex::account> &);
		lumex::uint128_t get_random_amount (secure::transaction const &, lumex::node &, lumex::account const &);
		void generate_rollback (lumex::node &, std::vector<lumex::account> &);
		void generate_change_known (lumex::node &, std::vector<lumex::account> &);
		void generate_change_unknown (lumex::node &, std::vector<lumex::account> &);
		void generate_receive (lumex::node &);
		void generate_send_new (lumex::node &, std::vector<lumex::account> &);
		void generate_send_existing (lumex::node &, std::vector<lumex::account> &);
		std::shared_ptr<lumex::state_block> upgrade_genesis_epoch (lumex::node &, lumex::epoch const);
		std::shared_ptr<lumex::wallet::wallet> wallet (size_t);
		/** Generate work with difficulty between \p min_difficulty_a (inclusive) and \p max_difficulty_a (exclusive) */
		uint64_t work_generate_limited (lumex::block_hash const & root_a, uint64_t min_difficulty_a, uint64_t max_difficulty_a);
		/**
		 * Polls, sleep if there's no work to be done (default 10ms), then check the deadline
		 * @returns 0 or lumex::deadline_expired
		 */
		std::error_code poll (std::chrono::lumexseconds const & sleep_time = std::chrono::milliseconds (10));
		std::error_code poll_until_true (std::chrono::lumexseconds deadline, std::function<bool ()>);
		void delay_ms (std::chrono::milliseconds const & delay);
		void deadline_set (std::chrono::duration<double, std::lumex> const & delta);
		/*
		 * Convenience function to get a reference to a node at given index. Does bound checking.
		 */
		lumex::node & node (std::size_t index) const;
		std::shared_ptr<lumex::node> add_node (lumex::transport::transport_type = lumex::transport::transport_type::tcp);
		std::shared_ptr<lumex::node> add_node (lumex::node_flags const &, lumex::transport::transport_type = lumex::transport::transport_type::tcp);
		std::shared_ptr<lumex::node> add_node (lumex::node_config const &, lumex::transport::transport_type = lumex::transport::transport_type::tcp);
		std::shared_ptr<lumex::node> add_node (lumex::node_config const &, lumex::node_flags const &, lumex::transport::transport_type = lumex::transport::transport_type::tcp, std::optional<lumex::keypair> const & rep = std::nullopt);

		// Make an independent node that uses system resources but is not part of the system node list and does not automatically connect to other nodes
		std::shared_ptr<lumex::node> make_disconnected_node ();
		std::shared_ptr<lumex::node> make_disconnected_node (lumex::node_config const &, lumex::node_flags const &);
		void register_node (std::shared_ptr<lumex::node> const &);
		void stop_node (lumex::node &);

		/*
		 * Returns default config for node running in test environment
		 */
		lumex::node_config default_config ();

		/*
		 * Returns port 0 by default, to let the O/S choose a port number.
		 * If LUMEX_TEST_BASE_PORT is set then it allocates numbers by itself from that range.
		 */
		uint16_t get_available_port ();

	private:
		void setup_node (lumex::node &);

	public:
		std::shared_ptr<boost::asio::io_context> io_ctx;
		boost::asio::executor_work_guard<boost::asio::io_context::executor_type> io_guard;
		std::vector<std::shared_ptr<lumex::node>> nodes;
		std::vector<std::shared_ptr<lumex::node>> disconnected_nodes;
		lumex::logger logger;
		lumex::stats stats;
		lumex::work_pool work{ lumex::dev::network_params.network, std::max (lumex::hardware_concurrency (), 1u) };
		std::chrono::time_point<std::chrono::steady_clock, std::chrono::duration<double>> deadline{ std::chrono::steady_clock::time_point::max () };
		double deadline_scaling_factor{ 1.0 };
		unsigned node_sequence{ 0 };
		std::deque<std::shared_ptr<lumex::block>> initialization_blocks;
		std::deque<std::shared_ptr<lumex::block>> initialization_blocks_cemented;
	};

	std::shared_ptr<lumex::state_block> upgrade_epoch (lumex::work_pool &, lumex::ledger &, lumex::epoch);
	void cleanup_dev_directories_on_exit ();
}
}
REGISTER_ERROR_CODES (lumex, error_system);
