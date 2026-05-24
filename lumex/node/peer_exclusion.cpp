#include <lumex/lib/assert.hpp>
#include <lumex/lib/container_info.hpp>
#include <lumex/node/peer_exclusion.hpp>

lumex::peer_exclusion::peer_exclusion (std::size_t max_size_a) :
	max_size{ max_size_a }
{
}

uint64_t lumex::peer_exclusion::add (lumex::tcp_endpoint const & endpoint)
{
	uint64_t result = 0;
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	if (auto existing = peers.get<tag_endpoint> ().find (endpoint.address ()); existing == peers.get<tag_endpoint> ().end ())
	{
		// Clean old excluded peers
		while (peers.size () > 1 && peers.size () >= max_size)
		{
			peers.get<tag_exclusion> ().erase (peers.get<tag_exclusion> ().begin ());
		}
		debug_assert (peers.size () <= max_size);

		// Insert new endpoint
		auto inserted = peers.insert (peer_exclusion::item{ std::chrono::steady_clock::steady_clock::now () + exclude_time_hours, endpoint.address (), 1 });
		(void)inserted;
		debug_assert (inserted.second);
		result = 1;
	}
	else
	{
		// Update existing endpoint
		peers.get<tag_endpoint> ().modify (existing, [&result] (peer_exclusion::item & item_a) {
			++item_a.score;
			result = item_a.score;
			if (item_a.score == peer_exclusion::score_limit)
			{
				item_a.exclude_until = std::chrono::steady_clock::now () + peer_exclusion::exclude_time_hours;
			}
			else if (item_a.score > peer_exclusion::score_limit)
			{
				item_a.exclude_until = std::chrono::steady_clock::now () + peer_exclusion::exclude_time_hours * item_a.score * 2;
			}
		});
	}
	return result;
}

uint64_t lumex::peer_exclusion::score (const lumex::tcp_endpoint & endpoint) const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	if (auto existing = peers.get<tag_endpoint> ().find (endpoint.address ()); existing != peers.get<tag_endpoint> ().end ())
	{
		return existing->score;
	}
	return 0;
}

std::chrono::steady_clock::time_point lumex::peer_exclusion::until (const lumex::tcp_endpoint & endpoint) const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	if (auto existing = peers.get<tag_endpoint> ().find (endpoint.address ()); existing != peers.get<tag_endpoint> ().end ())
	{
		return existing->exclude_until;
	}
	return {};
}

bool lumex::peer_exclusion::check (lumex::tcp_endpoint const & endpoint) const
{
	return check (endpoint.address ());
}

bool lumex::peer_exclusion::check (boost::asio::ip::address const & address) const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	if (auto existing = peers.get<tag_endpoint> ().find (address); existing != peers.get<tag_endpoint> ().end ())
	{
		if (existing->score >= score_limit && existing->exclude_until > std::chrono::steady_clock::now ())
		{
			return true;
		}
	}
	return false;
}

void lumex::peer_exclusion::remove (lumex::tcp_endpoint const & endpoint_a)
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	peers.get<tag_endpoint> ().erase (endpoint_a.address ());
}

std::size_t lumex::peer_exclusion::size () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return peers.size ();
}

lumex::container_info lumex::peer_exclusion::container_info () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	lumex::container_info info;
	info.put ("peers", peers.size ());
	return info;
}