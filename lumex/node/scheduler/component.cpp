#include <lumex/node/active_elections.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/scheduler/component.hpp>
#include <lumex/node/scheduler/hinted.hpp>
#include <lumex/node/scheduler/manual.hpp>
#include <lumex/node/scheduler/optimistic.hpp>
#include <lumex/node/scheduler/priority.hpp>

lumex::scheduler::component::component (lumex::node_config & node_config, lumex::node & node, lumex::ledger & ledger, lumex::ledger_notifications & ledger_notifications, lumex::bucketing & bucketing, lumex::active_elections & active, lumex::online_reps & online_reps, lumex::vote_cache & vote_cache, lumex::cementing_set & cementing_set, lumex::stats & stats, lumex::logger & logger) :
	hinted_impl{ std::make_unique<lumex::scheduler::hinted> (node_config.hinted_scheduler, node, vote_cache, active, online_reps, stats) },
	manual_impl{ std::make_unique<lumex::scheduler::manual> (node) },
	optimistic_impl{ std::make_unique<lumex::scheduler::optimistic> (node_config.optimistic_scheduler, node, ledger, active, node_config.network_params.network, stats) },
	priority_impl{ std::make_unique<lumex::scheduler::priority> (node_config, node, ledger, ledger_notifications, bucketing, active, cementing_set, stats, logger) },
	hinted{ *hinted_impl },
	manual{ *manual_impl },
	optimistic{ *optimistic_impl },
	priority{ *priority_impl }
{
	// Notify election schedulers when AEC frees election slot
	active.vacancy_updated.add ([this] () {
		priority.notify ();
		hinted.notify ();
		optimistic.notify ();
	});
}

lumex::scheduler::component::~component ()
{
}

void lumex::scheduler::component::start ()
{
	hinted.start ();
	manual.start ();
	optimistic.start ();
	priority.start ();
}

void lumex::scheduler::component::stop ()
{
	hinted.stop ();
	manual.stop ();
	optimistic.stop ();
	priority.stop ();
}

bool lumex::scheduler::component::contains (lumex::block_hash const & hash) const
{
	return manual.contains (hash) || priority.contains (hash);
}

lumex::container_info lumex::scheduler::component::container_info () const
{
	lumex::container_info info;
	info.add ("hinted", hinted.container_info ());
	info.add ("manual", manual.container_info ());
	info.add ("optimistic", optimistic.container_info ());
	info.add ("priority", priority.container_info ());
	return info;
}
