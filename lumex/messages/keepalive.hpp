#pragma once

#include <lumex/lib/network_types.hpp>
#include <lumex/messages/message.hpp>

#include <array>

namespace lumex::messages
{
/*
 * Binary Format:
 * [message_header] Common message header
 * [8x (16 bytes (IP) + 2 bytes (port)] Array of 8 peers
 *
 * Header extensions:
 * - No specific bits from the `extensions` field are used for `keepalive`.
 */
class keepalive final : public message
{
public:
	explicit keepalive (lumex::network_constants const & constants);
	keepalive (bool &, lumex::stream &, message_header const &);
	void visit (message_visitor &) const override;
	void serialize (lumex::stream &) const override;
	bool deserialize (lumex::stream &);
	bool operator== (keepalive const &) const;
	std::array<lumex::endpoint, 8> peers;
	static std::size_t constexpr size = 8 * (16 + 2);

public: // Logging
	void operator() (lumex::object_stream &) const override;
};
}
