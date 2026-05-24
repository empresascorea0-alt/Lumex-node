#pragma once

#include <lumex/lib/blocks.hpp>
#include <lumex/lib/fwd.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/fwd.hpp>

#include <cstddef>
#include <deque>
#include <memory>

namespace lumex
{
class ledger_rollback final : public lumex::block_visitor
{
public:
	ledger_rollback (lumex::secure::write_transaction const &, lumex::ledger &, std::deque<std::shared_ptr<lumex::block>> & list, size_t depth, size_t max_depth);

	void send_block (lumex::send_block const &) override;
	void receive_block (lumex::receive_block const &) override;
	void open_block (lumex::open_block const &) override;
	void change_block (lumex::change_block const &) override;
	void state_block (lumex::state_block const &) override;

	lumex::secure::write_transaction const & transaction;
	lumex::ledger & ledger;
	std::deque<std::shared_ptr<lumex::block>> & list;
	size_t const depth;
	size_t const max_depth;
	bool error{ false };
};
}