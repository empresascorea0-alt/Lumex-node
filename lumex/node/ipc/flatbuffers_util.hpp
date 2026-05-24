#pragma once

#include <lumex/ipc_flatbuffers_lib/generated/flatbuffers/lumexapi_generated.h>

#include <memory>

namespace lumex
{
class amount;
class block;
class send_block;
class receive_block;
class change_block;
class open_block;
class state_block;
namespace ipc
{
	/**
	 * Utilities to convert between blocks and Flatbuffers equivalents
	 */
	class flatbuffers_builder
	{
	public:
		static lumexapi::BlockUnion block_to_union (lumex::block const & block_a, lumex::amount const & amount_a, bool is_state_send_a = false, bool is_state_epoch_a = false);
		static std::unique_ptr<lumexapi::BlockStateT> from (lumex::state_block const & block_a, lumex::amount const & amount_a, bool is_state_send_a, bool is_state_epoch_a);
		static std::unique_ptr<lumexapi::BlockSendT> from (lumex::send_block const & block_a);
		static std::unique_ptr<lumexapi::BlockReceiveT> from (lumex::receive_block const & block_a);
		static std::unique_ptr<lumexapi::BlockOpenT> from (lumex::open_block const & block_a);
		static std::unique_ptr<lumexapi::BlockChangeT> from (lumex::change_block const & block_a);
	};
}
}
