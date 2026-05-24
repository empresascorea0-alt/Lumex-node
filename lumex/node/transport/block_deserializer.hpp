#pragma once

#include <lumex/lib/block_type.hpp>
#include <lumex/node/transport/fwd.hpp>

#include <boost/system/error_code.hpp>

#include <memory>
#include <vector>

namespace lumex::transport
{
/**
 * Class to read a block-type byte followed by a serialised block from a stream.
 * It is typically used to read a series of block-types and blocks terminated by a not-a-block type.
 */
class block_deserializer : public std::enable_shared_from_this<block_deserializer>
{
public:
	using callback_type = std::function<void (boost::system::error_code, std::shared_ptr<lumex::block>)>;

	block_deserializer ();
	/**
	 * Read a type-prefixed block from 'socket' and pass the result, or an error, to 'callback'
	 * A normal end to series of blocks is a marked by return no error and a nullptr for block.
	 */
	void read (lumex::transport::tcp_socket & socket, callback_type const && callback);

private:
	/**
	 * Called by read method on receipt of a block type byte.
	 * The type byte will be in the read_buffer.
	 */
	void received_type (lumex::transport::tcp_socket & socket, callback_type const && callback);

	/**
	 * Called by received_type when a block is received, it parses the block and calls the callback.
	 */
	void received_block (lumex::block_type type, callback_type const && callback);

	std::shared_ptr<std::vector<uint8_t>> read_buffer;
};
}
