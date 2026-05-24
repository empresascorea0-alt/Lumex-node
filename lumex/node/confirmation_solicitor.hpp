#pragma once

#include <lumex/node/network.hpp>
#include <lumex/node/repcrawler.hpp>

#include <unordered_map>

namespace lumex
{
class election;
class node;
class node_config;
/** This class accepts elections that need further votes before they can be confirmed and bundles them in to single confirm_req packets */
class confirmation_solicitor final
{
public:
	confirmation_solicitor (lumex::network &, lumex::node_config const &);
	/** Prepare object for batching election confirmation requests*/
	void prepare (std::vector<lumex::representative> const &);
	/** Broadcast the winner of an election if the broadcast limit has not been reached. Returns false if the broadcast was performed */
	bool broadcast (lumex::election const &);
	/** Add an election that needs to be confirmed. Returns false if successfully added */
	bool add (lumex::election const &);
	/** Dispatch bundled requests to each channel*/
	void flush ();
	/** Global maximum amount of block broadcasts */
	std::size_t const max_block_broadcasts;
	/** Maximum amount of requests to be sent per election, bypassed if an existing vote is for a different hash*/
	std::size_t const max_election_requests;
	/** Maximum amount of directed broadcasts to be sent per election */
	std::size_t const max_election_broadcasts;

private:
	lumex::network & network;
	lumex::node_config const & config;

	unsigned rebroadcasted{ 0 };
	std::vector<lumex::representative> representatives_requests;
	std::vector<lumex::representative> representatives_broadcasts;
	using vector_root_hashes = std::vector<std::pair<lumex::block_hash, lumex::root>>;
	std::unordered_map<std::shared_ptr<lumex::transport::channel>, vector_root_hashes> requests;
	bool prepared{ false };
};
}
