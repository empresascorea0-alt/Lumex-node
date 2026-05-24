#include <lumex/lib/assert.hpp>
#include <lumex/lib/block_type.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/node/ipc/flatbuffers_util.hpp>
#include <lumex/secure/common.hpp>

std::unique_ptr<lumexapi::BlockStateT> lumex::ipc::flatbuffers_builder::from (lumex::state_block const & block_a, lumex::amount const & amount_a, bool is_state_send_a, bool is_state_epoch_a)
{
	auto block (std::make_unique<lumexapi::BlockStateT> ());
	block->account = block_a.account ().to_account ();
	block->hash = block_a.hash ().to_string ();
	block->previous = block_a.previous ().to_string ();
	block->representative = block_a.representative_field ().value ().to_account ();
	block->balance = block_a.balance ().to_string_dec ();
	block->link = block_a.link_field ().value ().to_string ();
	block->link_as_account = block_a.link_field ().value ().to_account ();
	block->signature = block_a.signature.to_string ();
	block->work = lumex::to_string_hex (block_a.work);

	if (is_state_send_a)
	{
		block->subtype = lumexapi::BlockSubType::BlockSubType_send;
	}
	else if (block_a.is_change ())
	{
		block->subtype = lumexapi::BlockSubType::BlockSubType_change;
	}
	else if (amount_a == 0 && is_state_epoch_a)
	{
		block->subtype = lumexapi::BlockSubType::BlockSubType_epoch;
	}
	else
	{
		block->subtype = lumexapi::BlockSubType::BlockSubType_receive;
	}
	return block;
}

std::unique_ptr<lumexapi::BlockSendT> lumex::ipc::flatbuffers_builder::from (lumex::send_block const & block_a)
{
	auto block (std::make_unique<lumexapi::BlockSendT> ());
	block->hash = block_a.hash ().to_string ();
	block->balance = block_a.balance ().to_string_dec ();
	block->destination = block_a.hashables.destination.to_account ();
	block->previous = block_a.previous ().to_string ();
	block->signature = block_a.signature.to_string ();
	block->work = lumex::to_string_hex (block_a.work);
	return block;
}

std::unique_ptr<lumexapi::BlockReceiveT> lumex::ipc::flatbuffers_builder::from (lumex::receive_block const & block_a)
{
	auto block (std::make_unique<lumexapi::BlockReceiveT> ());
	block->hash = block_a.hash ().to_string ();
	block->source = block_a.source_field ().value ().to_string ();
	block->previous = block_a.previous ().to_string ();
	block->signature = block_a.signature.to_string ();
	block->work = lumex::to_string_hex (block_a.work);
	return block;
}

std::unique_ptr<lumexapi::BlockOpenT> lumex::ipc::flatbuffers_builder::from (lumex::open_block const & block_a)
{
	auto block (std::make_unique<lumexapi::BlockOpenT> ());
	block->hash = block_a.hash ().to_string ();
	block->source = block_a.source_field ().value ().to_string ();
	block->account = block_a.account ().to_account ();
	block->representative = block_a.representative_field ().value ().to_account ();
	block->signature = block_a.signature.to_string ();
	block->work = lumex::to_string_hex (block_a.work);
	return block;
}

std::unique_ptr<lumexapi::BlockChangeT> lumex::ipc::flatbuffers_builder::from (lumex::change_block const & block_a)
{
	auto block (std::make_unique<lumexapi::BlockChangeT> ());
	block->hash = block_a.hash ().to_string ();
	block->previous = block_a.previous ().to_string ();
	block->representative = block_a.representative_field ().value ().to_account ();
	block->signature = block_a.signature.to_string ();
	block->work = lumex::to_string_hex (block_a.work);
	return block;
}

lumexapi::BlockUnion lumex::ipc::flatbuffers_builder::block_to_union (lumex::block const & block_a, lumex::amount const & amount_a, bool is_state_send_a, bool is_state_epoch_a)
{
	lumexapi::BlockUnion u;
	switch (block_a.type ())
	{
		case lumex::block_type::state:
		{
			u.Set (*from (dynamic_cast<lumex::state_block const &> (block_a), amount_a, is_state_send_a, is_state_epoch_a));
			break;
		}
		case lumex::block_type::send:
		{
			u.Set (*from (dynamic_cast<lumex::send_block const &> (block_a)));
			break;
		}
		case lumex::block_type::receive:
		{
			u.Set (*from (dynamic_cast<lumex::receive_block const &> (block_a)));
			break;
		}
		case lumex::block_type::open:
		{
			u.Set (*from (dynamic_cast<lumex::open_block const &> (block_a)));
			break;
		}
		case lumex::block_type::change:
		{
			u.Set (*from (dynamic_cast<lumex::change_block const &> (block_a)));
			break;
		}

		default:
			debug_assert (false);
	}
	return u;
}
