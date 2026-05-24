#pragma once

#include <lumex/lib/logging.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/work.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/store/fwd.hpp>
#include <lumex/store/ledger_store.hpp>

namespace lumex::test
{
class ledger_context
{
public:
	/** 'blocks' initialises the ledger with each block in-order
		Blocks must all return process_result::progress when processed */
	ledger_context (std::deque<std::shared_ptr<lumex::block>> && blocks = std::deque<std::shared_ptr<lumex::block>>{});

	lumex::ledger & ledger ();
	lumex::store::ledger_store & store ();
	std::deque<std::shared_ptr<lumex::block>> const & blocks () const;
	lumex::work_pool & pool ();
	lumex::stats & stats ();
	lumex::logger & logger ();

private:
	lumex::logger logger_m;
	lumex::stats stats_m{ logger_m };
	std::unique_ptr<lumex::store::ledger_store> store_m;
	lumex::ledger ledger_m;
	std::deque<std::shared_ptr<lumex::block>> blocks_m;
	lumex::work_pool pool_m;
};

/** Only a genesis block */
ledger_context ledger_empty ();
/** Send/receive pair of state blocks on the genesis account */
ledger_context ledger_send_receive ();
/** Send/receive pair of legacy blocks on the genesis account */
ledger_context ledger_send_receive_legacy ();
/** Full binary tree of state blocks */
ledger_context ledger_diamond (unsigned height);
/** Single chain of state blocks with send and receives to itself */
ledger_context ledger_single_chain (unsigned height);
}
