#pragma once

#include <lumex/lib/config.hpp>
#include <lumex/lib/constants.hpp>

#include <chrono>

namespace lumex::transport
{
class tcp_config
{
public:
	explicit tcp_config (lumex::network_constants const & network)
	{
		if (network.is_dev_network ())
		{
			max_inbound_connections = 128;
			max_outbound_connections = 128;
			max_attempts = 128;
			max_attempts_per_ip = 128;
			connect_timeout = 5s;
			checkup_interval = 1s;
		}
	}

public:
	lumex::error deserialize (lumex::tomlconfig &);
	lumex::error serialize (lumex::tomlconfig &) const;

public:
	size_t max_inbound_connections{ 2048 };
	size_t max_outbound_connections{ 2048 };
	size_t max_attempts{ 60 };
	size_t max_attempts_per_ip{ 1 };
	std::chrono::seconds connect_timeout{ 30 };
	std::chrono::seconds handshake_timeout{ 30 };
	std::chrono::seconds io_timeout{ 30 };
	std::chrono::seconds silent_timeout{ 30 };
	std::chrono::seconds checkup_interval{ 5 };
};
}