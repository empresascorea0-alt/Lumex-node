#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/node/fwd.hpp>

#include <memory>
#include <string>

namespace lumex::scheduler
{
class component final
{
public:
	component (lumex::node_config &, lumex::node &, lumex::ledger &, lumex::ledger_notifications &, lumex::bucketing &, lumex::active_elections &, lumex::online_reps &, lumex::vote_cache &, lumex::cementing_set &, lumex::stats &, lumex::logger &);
	~component ();

	void start ();
	void stop ();

	/// Does the block exist in any of the schedulers
	bool contains (lumex::block_hash const & hash) const;

	lumex::container_info container_info () const;

private:
	std::unique_ptr<lumex::scheduler::hinted> hinted_impl;
	std::unique_ptr<lumex::scheduler::manual> manual_impl;
	std::unique_ptr<lumex::scheduler::optimistic> optimistic_impl;
	std::unique_ptr<lumex::scheduler::priority> priority_impl;

public: // Schedulers
	lumex::scheduler::hinted & hinted;
	lumex::scheduler::manual & manual;
	lumex::scheduler::optimistic & optimistic;
	lumex::scheduler::priority & priority;
};
}
