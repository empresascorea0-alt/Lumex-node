#include <lumex/lib/assert.hpp>
#include <lumex/lib/networks.hpp>

#include <boost/algorithm/string/case_conv.hpp>

namespace
{
// Initial value is ACTIVE_NETWORK compile flag, but can be overridden by CLI/API
lumex::network_type & active_network_singleton ()
{
	static lumex::network_type instance{ lumex::network_type::ACTIVE_NETWORK };
	return instance;
}
}

namespace lumex
{
lumex::network_type get_active_network ()
{
	return active_network_singleton ();
}

void set_active_network (lumex::network_type network)
{
	active_network_singleton () = network;
}

std::string_view to_string (lumex::network_type network)
{
	switch (network)
	{
		case lumex::network_type::lumex_beta_network:
			return "beta";
		case lumex::network_type::lumex_dev_network:
			return "dev";
		case lumex::network_type::lumex_live_network:
			return "live";
		case lumex::network_type::lumex_test_network:
			return "test";
		case lumex::network_type::invalid:
			return "invalid";
	}
	release_assert (false, "invalid network");
}

std::optional<lumex::network_type> parse_network (std::string network_name)
{
	boost::algorithm::to_lower (network_name);

	if (network_name == "live")
	{
		return lumex::network_type::lumex_live_network;
	}
	if (network_name == "beta")
	{
		return lumex::network_type::lumex_beta_network;
	}
	if (network_name == "dev")
	{
		return lumex::network_type::lumex_dev_network;
	}
	if (network_name == "test")
	{
		return lumex::network_type::lumex_test_network;
	}
	return std::nullopt;
}
}
