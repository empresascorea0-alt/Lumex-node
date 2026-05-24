#include <lumex/lib/object_stream.hpp>
#include <lumex/lib/stream.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/messages/bulk_pull_account.hpp>
#include <lumex/messages/message_visitor.hpp>

namespace lumex::messages
{
bulk_pull_account::bulk_pull_account (lumex::network_constants const & constants) :
	message (constants, message_type::bulk_pull_account)
{
}

bulk_pull_account::bulk_pull_account (bool & error_a, lumex::stream & stream_a, message_header const & header_a) :
	message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void bulk_pull_account::visit (message_visitor & visitor_a) const
{
	visitor_a.bulk_pull_account (*this);
}

void bulk_pull_account::serialize (lumex::stream & stream_a) const
{
	header.serialize (stream_a);
	write (stream_a, account);
	write (stream_a, minimum_amount);
	write (stream_a, flags);
}

bool bulk_pull_account::deserialize (lumex::stream & stream_a)
{
	debug_assert (header.type == message_type::bulk_pull_account);
	auto error (false);
	try
	{
		lumex::read (stream_a, account);
		lumex::read (stream_a, minimum_amount);
		lumex::read (stream_a, flags);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void bulk_pull_account::operator() (lumex::object_stream & obs) const
{
	message::operator() (obs); // Write common data

	obs.write ("account", account);
	obs.write ("minimum_amount", minimum_amount);
	obs.write ("flags", static_cast<uint8_t> (flags)); // TODO: Prettier flag printing
}
}
