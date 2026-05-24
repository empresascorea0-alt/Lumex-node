#pragma once

#include <lumex/node/fair_queue.hpp>
#include <lumex/node/transport/channel.hpp>

namespace lumex
{
template <>
struct fair_queue_traits<std::shared_ptr<lumex::transport::channel>>
{
	static bool alive (std::shared_ptr<lumex::transport::channel> const & channel)
	{
		return !channel || channel->alive ();
	}
};
}
