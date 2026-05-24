#include <lumex/lib/object_stream.hpp>
#include <lumex/lib/stream.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/messages/frontier_req.hpp>
#include <lumex/messages/message_visitor.hpp>

namespace lumex::messages
{
frontier_req::frontier_req (lumex::network_constants const & constants) :
	message (constants, message_type::frontier_req)
{
}

frontier_req::frontier_req (bool & error_a, lumex::stream & stream_a, message_header const & header_a) :
	message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void frontier_req::serialize (lumex::stream & stream_a) const
{
	header.serialize (stream_a);
	write (stream_a, start.bytes);
	write (stream_a, age);
	write (stream_a, count);
}

bool frontier_req::deserialize (lumex::stream & stream_a)
{
	debug_assert (header.type == message_type::frontier_req);
	auto error (false);
	try
	{
		lumex::read (stream_a, start.bytes);
		lumex::read (stream_a, age);
		lumex::read (stream_a, count);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void frontier_req::visit (message_visitor & visitor_a) const
{
	visitor_a.frontier_req (*this);
}

bool frontier_req::operator== (frontier_req const & other_a) const
{
	return start == other_a.start && age == other_a.age && count == other_a.count;
}

void frontier_req::operator() (lumex::object_stream & obs) const
{
	message::operator() (obs); // Write common data

	obs.write ("start", start);
	obs.write ("age", age);
	obs.write ("count", count);
}
}
