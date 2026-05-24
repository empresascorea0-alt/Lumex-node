#pragma once

#include <lumex/lib/fwd.hpp>
#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/secure/fwd.hpp>
#include <lumex/store/fwd.hpp>

#include <cstdint>
#include <future>

namespace lumex
{
class epoch_upgrader final
{
public:
	epoch_upgrader (lumex::node &, lumex::ledger &, lumex::store::ledger_store &, lumex::network_params &, lumex::logger &);

	bool start (lumex::raw_key const & prv, lumex::epoch epoch, uint64_t count_limit, uint64_t threads);
	void stop ();

private: // Dependencies
	lumex::node & node;
	lumex::ledger & ledger;
	lumex::store::ledger_store & store;
	lumex::network_params & network_params;
	lumex::logger & logger;

private:
	void upgrade_impl (lumex::raw_key const & prv, lumex::epoch epoch, uint64_t count_limit, uint64_t threads);

	std::atomic<bool> stopped{ false };
	lumex::locked<std::future<void>> epoch_upgrading;
};
}
