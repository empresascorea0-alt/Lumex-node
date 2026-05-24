#include <lumex/lib/object_stream.hpp>
#include <lumex/lib/stream.hpp>
#include <lumex/messages/bulk_push.hpp>
#include <lumex/messages/message_visitor.hpp>

namespace lumex::messages
{
bulk_push::bulk_push (lumex::network_constants const & constants) :
	message (constants, message_type::bulk_push)
{
}

bulk_push::bulk_push (message_header const & header_a) :
	message (header_a)
{
}

bool bulk_push::deserialize (lumex::stream & stream_a)
{
	debug_assert (header.type == message_type::bulk_push);
	return false;
}

void bulk_push::serialize (lumex::stream & stream_a) const
{
	header.serialize (stream_a);
}

void bulk_push::visit (message_visitor & visitor_a) const
{
	visitor_a.bulk_push (*this);
}

void bulk_push::operator() (lumex::object_stream & obs) const
{
	message::operator() (obs); // Write common data
}
}
