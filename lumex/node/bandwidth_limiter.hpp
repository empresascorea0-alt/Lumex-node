#pragma once

#include <lumex/lib/rate_limiting.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/node/transport/traffic_type.hpp>

namespace lumex
{
class bandwidth_limiter_config final
{
public:
	explicit bandwidth_limiter_config (lumex::node_config const &);

public:
	std::size_t generic_limit;
	double generic_burst_ratio;

	std::size_t bootstrap_limit;
	double bootstrap_burst_ratio;
};

/**
 * Class that tracks and manages bandwidth limits for IO operations
 */
class bandwidth_limiter final
{
public:
	bandwidth_limiter (lumex::node_config const &, lumex::node_flags const &);

	/**
	 * Check whether packet falls withing bandwidth limits and should be allowed
	 * @return true if OK, false if needs to be dropped
	 */
	bool should_pass (std::size_t buffer_size, lumex::transport::traffic_type type);
	/**
	 * Reset limits of selected limiter type to values passed in arguments
	 */
	void reset (std::size_t limit, double burst_ratio, lumex::transport::traffic_type type = lumex::transport::traffic_type::generic);

	lumex::container_info container_info () const;

	std::pair<std::size_t, double> get_limit (lumex::transport::traffic_type type = lumex::transport::traffic_type::generic) const;

private:
	/**
	 * Returns reference to limiter corresponding to the limit type
	 */
	lumex::rate_limiter & select_limiter (lumex::transport::traffic_type type);
	lumex::rate_limiter const & select_limiter (lumex::transport::traffic_type type) const;

private:
	bandwidth_limiter_config const config;

private:
	lumex::rate_limiter limiter_generic;
	lumex::rate_limiter limiter_bootstrap;
};
}
