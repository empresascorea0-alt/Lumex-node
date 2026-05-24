#include <lumex/lib/utility.hpp>
#include <lumex/node/bandwidth_limiter.hpp>
#include <lumex/node/nodeconfig.hpp>

/*
 * bandwidth_limiter
 */

lumex::bandwidth_limiter::bandwidth_limiter (lumex::node_config const & node_config, lumex::node_flags const & flags) :
	config{ node_config },
	// In super_rebroadcaster mode, use unlimited bandwidth (0 = no limit)
	limiter_generic{ flags.super_rebroadcaster ? 0 : config.generic_limit, config.generic_burst_ratio },
	limiter_bootstrap{ config.bootstrap_limit, config.bootstrap_burst_ratio }
{
}

lumex::rate_limiter & lumex::bandwidth_limiter::select_limiter (lumex::transport::traffic_type type)
{
	switch (type)
	{
		case lumex::transport::traffic_type::bootstrap_server:
			return limiter_bootstrap;
		default:
			return limiter_generic;
	}
}

lumex::rate_limiter const & lumex::bandwidth_limiter::select_limiter (lumex::transport::traffic_type type) const
{
	switch (type)
	{
		case lumex::transport::traffic_type::bootstrap_server:
			return limiter_bootstrap;
		default:
			return limiter_generic;
	}
}

bool lumex::bandwidth_limiter::should_pass (std::size_t buffer_size, lumex::transport::traffic_type type)
{
	auto & limiter = select_limiter (type);
	return limiter.should_pass (buffer_size);
}

void lumex::bandwidth_limiter::reset (std::size_t limit, double burst_ratio, lumex::transport::traffic_type type)
{
	auto & limiter = select_limiter (type);
	limiter.reset (limit, burst_ratio);
}

lumex::container_info lumex::bandwidth_limiter::container_info () const
{
	lumex::container_info info;
	info.put ("generic", limiter_generic.size ());
	info.put ("bootstrap", limiter_bootstrap.size ());
	return info;
}

std::pair<std::size_t, double> lumex::bandwidth_limiter::get_limit (lumex::transport::traffic_type type) const
{
	return select_limiter (type).get_limit ();
}

/*
 * bandwidth_limiter_config
 */

lumex::bandwidth_limiter_config::bandwidth_limiter_config (lumex::node_config const & node_config) :
	generic_limit{ node_config.bandwidth_limit },
	generic_burst_ratio{ node_config.bandwidth_limit_burst_ratio },
	bootstrap_limit{ node_config.bootstrap_bandwidth_limit },
	bootstrap_burst_ratio{ node_config.bootstrap_bandwidth_burst_ratio }
{
}