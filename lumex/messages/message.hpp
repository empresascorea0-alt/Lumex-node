#pragma once

#include <lumex/lib/asio.hpp>
#include <lumex/lib/fwd.hpp>
#include <lumex/messages/common.hpp>
#include <lumex/messages/fwd.hpp>
#include <lumex/messages/message_header.hpp>

#include <memory>
#include <vector>

namespace lumex::messages
{
class message
{
public:
	explicit message (lumex::network_constants const &, message_type);
	explicit message (message_header const &);
	virtual ~message () = default;

	virtual void serialize (lumex::stream &) const = 0;
	virtual void visit (message_visitor &) const = 0;

	std::shared_ptr<std::vector<uint8_t>> to_bytes () const;
	lumex::shared_const_buffer to_shared_const_buffer () const;

	message_type type () const;

public:
	message_header header;

public: // Logging
	virtual void operator() (lumex::object_stream &) const;
};
}
