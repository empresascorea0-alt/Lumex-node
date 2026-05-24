#pragma once

#include <lumex/lib/network_filter.hpp>
#include <lumex/messages/fwd.hpp>
#include <lumex/messages/message.hpp>

#include <memory>

namespace lumex::messages
{
/*
 * Binary Format:
 * [message_header] Common message header
 * [variable] Block (serialized according to the block type specified in the header)
 *
 * Header extensions:
 * - [0x0f00] Block type: Identifies the specific type of the block.
 * - [0x0004] Originator flag
 */
class publish final : public message
{
public:
	publish (bool &, lumex::stream &, message_header const &, lumex::network_filter::digest_t const & digest = 0, lumex::block_uniquer * = nullptr);
	publish (lumex::network_constants const & constants, std::shared_ptr<lumex::block> const &, bool is_originator = false);

	void serialize (lumex::stream &) const override;
	bool deserialize (lumex::stream &, lumex::block_uniquer * = nullptr);
	void visit (message_visitor &) const override;
	bool operator== (publish const &) const;

	static uint8_t constexpr originator_flag = 2; // 0x0004
	bool is_originator () const;

public: // Payload
	std::shared_ptr<lumex::block> block;

	// Messages deserialized from network should have their digest set
	lumex::network_filter::digest_t digest{ 0 };

public: // Logging
	void operator() (lumex::object_stream &) const override;
};
}
