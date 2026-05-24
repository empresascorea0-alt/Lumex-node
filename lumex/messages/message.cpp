#include <lumex/lib/object_stream.hpp>
#include <lumex/lib/stream.hpp>
#include <lumex/messages/message.hpp>

namespace lumex::messages
{
message::message (lumex::network_constants const & constants, message_type type_a) :
	header (constants, type_a)
{
}

message::message (message_header const & header_a) :
	header (header_a)
{
}

std::shared_ptr<std::vector<uint8_t>> message::to_bytes () const
{
	auto bytes = std::make_shared<std::vector<uint8_t>> ();
	lumex::vectorstream stream (*bytes);
	serialize (stream);
	return bytes;
}

lumex::shared_const_buffer message::to_shared_const_buffer () const
{
	return shared_const_buffer (to_bytes ());
}

message_type message::type () const
{
	return header.type;
}

void message::operator() (lumex::object_stream & obs) const
{
	obs.write ("header", header);
}
}
