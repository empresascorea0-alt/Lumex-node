#pragma once

#include <lumex/lib/assert.hpp>
#include <lumex/lib/stream.hpp>

namespace lumex::messages
{
struct empty_payload
{
	void serialize (lumex::stream &) const
	{
		debug_assert (false);
	}
	void deserialize (lumex::stream &)
	{
		debug_assert (false);
	}
	void operator() (lumex::object_stream &) const
	{
		debug_assert (false);
	}
};
}