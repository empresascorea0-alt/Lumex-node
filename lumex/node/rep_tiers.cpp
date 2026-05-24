#include <lumex/lib/enum_util.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/thread_roles.hpp>
#include <lumex/node/online_reps.hpp>
#include <lumex/node/rep_tiers.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/ledger.hpp>

using namespace std::chrono_literals;

lumex::rep_tiers::rep_tiers (lumex::ledger & ledger_a, lumex::network_params & network_params_a, lumex::online_reps & online_reps_a, lumex::stats & stats_a, lumex::logger & logger_a) :
	ledger{ ledger_a },
	network_params{ network_params_a },
	online_reps{ online_reps_a },
	stats{ stats_a },
	logger{ logger_a }
{
}

lumex::rep_tiers::~rep_tiers ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void lumex::rep_tiers::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		lumex::thread_role::set (lumex::thread_role::name::rep_tiers);
		run ();
	} };
}

void lumex::rep_tiers::stop ()
{
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

lumex::rep_tier lumex::rep_tiers::tier (const lumex::account & representative) const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	if (representatives_3.find (representative) != representatives_3.end ())
	{
		return lumex::rep_tier::tier_3;
	}
	if (representatives_2.find (representative) != representatives_2.end ())
	{
		return lumex::rep_tier::tier_2;
	}
	if (representatives_1.find (representative) != representatives_1.end ())
	{
		return lumex::rep_tier::tier_1;
	}
	return lumex::rep_tier::none;
}

void lumex::rep_tiers::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (lumex::stat::type::rep_tiers, lumex::stat::detail::loop);

		lock.unlock ();

		calculate_tiers ();

		lock.lock ();

		std::chrono::milliseconds interval = network_params.network.is_dev_network () ? 500ms : 10min;
		condition.wait_for (lock, interval);
	}
}

void lumex::rep_tiers::calculate_tiers ()
{
	auto stake = online_reps.trended ();
	auto rep_amounts = ledger.rep_weights_snapshot ();

	decltype (representatives_1) representatives_1_l;
	decltype (representatives_2) representatives_2_l;
	decltype (representatives_3) representatives_3_l;

	int ignored = 0;
	for (auto const & rep_amount : rep_amounts)
	{
		lumex::account const & representative = rep_amount.first;

		// Using ledger weight here because it takes preconfigured bootstrap weights into account
		auto weight = ledger.weight (representative);
		if (weight > stake / 1000) // 0.1% or above (level 1)
		{
			representatives_1_l.insert (representative);
			if (weight > stake / 100) // 1% or above (level 2)
			{
				representatives_2_l.insert (representative);
				if (weight > stake / 20) // 5% or above (level 3)
				{
					representatives_3_l.insert (representative);
				}
			}
		}
		else
		{
			++ignored;
		}
	}

	stats.add (lumex::stat::type::rep_tiers, lumex::stat::detail::processed, lumex::stat::dir::in, rep_amounts.size ());
	stats.add (lumex::stat::type::rep_tiers, lumex::stat::detail::ignored, lumex::stat::dir::in, ignored);

	logger.debug (lumex::log::type::rep_tiers, "Representative tiers updated, tier 1: {}, tier 2: {}, tier 3: {} ({} ignored)",
	representatives_1_l.size (),
	representatives_2_l.size (),
	representatives_3_l.size (),
	ignored);

	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		representatives_1 = std::move (representatives_1_l);
		representatives_2 = std::move (representatives_2_l);
		representatives_3 = std::move (representatives_3_l);
	}

	stats.inc (lumex::stat::type::rep_tiers, lumex::stat::detail::updated);
}

lumex::container_info lumex::rep_tiers::container_info () const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };

	lumex::container_info info;
	info.put ("tier_1", representatives_1);
	info.put ("tier_2", representatives_2);
	info.put ("tier_3", representatives_3);
	return info;
}

lumex::stat::detail lumex::to_stat_detail (lumex::rep_tier tier)
{
	return lumex::enum_convert<lumex::stat::detail> (tier);
}
