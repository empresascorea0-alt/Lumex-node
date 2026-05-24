#include <lumex/lib/blocks.hpp>
#include <lumex/messages/confirm.hpp>
#include <lumex/messages/publish.hpp>
#include <lumex/node/confirmation_solicitor.hpp>
#include <lumex/node/election.hpp>
#include <lumex/node/nodeconfig.hpp>

using namespace std::chrono_literals;

lumex::confirmation_solicitor::confirmation_solicitor (lumex::network & network_a, lumex::node_config const & config_a) :
	max_block_broadcasts (config_a.network_params.network.is_dev_network () ? 4 : 30),
	max_election_requests (50),
	max_election_broadcasts (std::max<std::size_t> (network_a.fanout () / 2, 1)),
	network (network_a),
	config (config_a)
{
}

void lumex::confirmation_solicitor::prepare (std::vector<lumex::representative> const & representatives_a)
{
	debug_assert (!prepared);
	debug_assert (std::none_of (representatives_a.begin (), representatives_a.end (), [] (auto const & rep) { return rep.channel == nullptr; }));

	requests.clear ();
	rebroadcasted = 0;
	/** Two copies are required as representatives can be erased from \p representatives_requests */
	representatives_requests = representatives_a;
	representatives_broadcasts = representatives_a;
	prepared = true;
}

bool lumex::confirmation_solicitor::broadcast (lumex::election const & election_a)
{
	debug_assert (prepared);
	bool error (true);
	if (rebroadcasted++ < max_block_broadcasts)
	{
		auto const & hash (election_a.status.winner->hash ());
		lumex::messages::publish winner{ config.network_params.network, election_a.status.winner };
		unsigned count = 0;
		// Directed broadcasting to principal representatives
		for (auto i (representatives_broadcasts.begin ()), n (representatives_broadcasts.end ()); i != n && count < max_election_broadcasts; ++i)
		{
			auto existing (election_a.last_votes.find (i->account));
			bool const exists (existing != election_a.last_votes.end ());
			bool const different (exists && existing->second.hash != hash);
			if (!exists || different)
			{
				i->channel->send (winner, lumex::transport::traffic_type::block_broadcast);
				count += different ? 0 : 1;
			}
		}
		error = false;
	}
	return error;
}

bool lumex::confirmation_solicitor::add (lumex::election const & election_a)
{
	debug_assert (prepared);
	bool error (true);
	unsigned count = 0;
	auto const & hash (election_a.status.winner->hash ());
	for (auto i (representatives_requests.begin ()); i != representatives_requests.end () && count < max_election_requests;)
	{
		bool full_queue (false);
		auto rep (*i);
		auto existing (election_a.last_votes.find (rep.account));
		bool const exists (existing != election_a.last_votes.end ());
		bool const is_final (exists && (!election_a.is_quorum.load () || existing->second.timestamp == std::numeric_limits<uint64_t>::max ()));
		bool const different (exists && existing->second.hash != hash);
		if (!exists || !is_final || different)
		{
			if (!rep.channel->max (lumex::transport::traffic_type::confirmation_requests))
			{
				auto & request_queue (requests[rep.channel]);
				request_queue.emplace_back (election_a.status.winner->hash (), election_a.status.winner->root ());
				count += different ? 0 : 1;
				error = false;
			}
			else
			{
				full_queue = true;
			}
		}
		i = !full_queue ? i + 1 : representatives_requests.erase (i);
	}
	return error;
}

void lumex::confirmation_solicitor::flush ()
{
	debug_assert (prepared);
	for (auto const & request_queue : requests)
	{
		auto const & channel (request_queue.first);
		std::vector<std::pair<lumex::block_hash, lumex::root>> roots_hashes_l;
		for (auto const & root_hash : request_queue.second)
		{
			roots_hashes_l.push_back (root_hash);
			if (roots_hashes_l.size () == lumex::network::confirm_req_hashes_max)
			{
				lumex::messages::confirm_req req{ config.network_params.network, roots_hashes_l };
				channel->send (req, lumex::transport::traffic_type::confirmation_requests);
				roots_hashes_l.clear ();
			}
		}
		if (!roots_hashes_l.empty ())
		{
			lumex::messages::confirm_req req{ config.network_params.network, roots_hashes_l };
			channel->send (req, lumex::transport::traffic_type::confirmation_requests);
		}
	}
	prepared = false;
}
