#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/messages/message.hpp>

#include <cstdint>

namespace lumex::messages
{
class frontier_req final : public message
{
public:
	explicit frontier_req (lumex::network_constants const & constants);
	frontier_req (bool &, lumex::stream &, message_header const &);
	void serialize (lumex::stream &) const override;
	bool deserialize (lumex::stream &);
	void visit (message_visitor &) const override;
	bool operator== (frontier_req const &) const;
	lumex::account start;
	uint32_t age;
	uint32_t count;
	static std::size_t constexpr size = sizeof (start) + sizeof (age) + sizeof (count);

public: // Logging
	void operator() (lumex::object_stream &) const override;
};
}
