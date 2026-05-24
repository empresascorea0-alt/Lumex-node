#pragma once

#include <lumex/messages/message.hpp>

namespace lumex::messages
{
class bulk_push final : public message
{
public:
	explicit bulk_push (lumex::network_constants const & constants);
	explicit bulk_push (message_header const &);
	void serialize (lumex::stream &) const override;
	bool deserialize (lumex::stream &);
	void visit (message_visitor &) const override;

public: // Logging
	void operator() (lumex::object_stream &) const override;
};
}
