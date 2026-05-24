#pragma once

#include <lumex/messages/fwd.hpp>
#include <lumex/node/fwd.hpp>

#include <boost/property_tree/ptree_fwd.hpp>

#include <memory>

namespace lumex
{
class bootstrap_service
{
public:
	bootstrap_service (lumex::node_config const &, lumex::ledger &, lumex::ledger_notifications &, lumex::block_processor &, lumex::network &, lumex::stats &, lumex::logger &);
	~bootstrap_service ();

	void start ();
	void stop ();

	/**
	 * Process bootstrap messages coming from the network
	 */
	void process (lumex::messages::asc_pull_ack const & message, std::shared_ptr<lumex::transport::channel> const &);

	/**
	 * Clears priority and blocking accounts state
	 */
	void reset ();

	/**
	 * Adds an account to the priority set
	 */
	void prioritize (lumex::account const & account);

	std::size_t blocked_size () const;
	std::size_t priority_size () const;
	std::size_t score_size () const;

	bool prioritized (lumex::account const &) const;
	bool blocked (lumex::account const &) const;

	boost::property_tree::ptree info () const;

	struct status_result
	{
		size_t priorities;
		size_t blocking;
	};
	status_result status () const;

	lumex::container_info container_info () const;

private: // Dependencies
	lumex::node_config const & config;
	lumex::ledger & ledger;
	lumex::ledger_notifications & ledger_notifications;
	lumex::block_processor & block_processor;
	lumex::network & network;
	lumex::stats & stats;
	lumex::logger & logger;

private:
	std::unique_ptr<lumex::bootstrap::bootstrap_context> ctx_impl;
	lumex::bootstrap::bootstrap_context & ctx;
};
}
