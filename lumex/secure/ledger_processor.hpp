#pragma once

#include <lumex/lib/blocks.hpp>
#include <lumex/lib/fwd.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/fwd.hpp>

namespace lumex
{
class ledger_processor final : public lumex::mutable_block_visitor
{
public:
	ledger_processor (lumex::secure::write_transaction const &, lumex::ledger &);

	void send_block (lumex::send_block &) override;
	void receive_block (lumex::receive_block &) override;
	void open_block (lumex::open_block &) override;
	void change_block (lumex::change_block &) override;
	void state_block (lumex::state_block &) override;

	void state_block_impl (lumex::state_block &);
	void epoch_block_impl (lumex::state_block &);

	lumex::secure::write_transaction const & transaction;
	lumex::ledger & ledger;
	lumex::block_status result{ lumex::block_status::invalid };

private:
	bool validate_epoch_block (lumex::state_block const & block);

	// Returns 1 + max(deps' topo_height) or 0 (unindexed sentinel) when the index is disabled or any dependency is itself unindexed
	uint64_t topology_height (std::shared_ptr<lumex::block> const & dep1, std::shared_ptr<lumex::block> const & dep2 = nullptr) const;
};
}