#pragma once

#include <lumex/lib/constants.hpp>
#include <lumex/lib/errors.hpp>

#include <memory>

namespace lumex
{
class tomlconfig;
namespace websocket
{
	/** websocket configuration */
	class config final
	{
	public:
		config (lumex::network_constants const &);

		lumex::error deserialize_toml (lumex::tomlconfig &);
		lumex::error serialize_toml (lumex::tomlconfig &) const;

		lumex::network_constants const & network_constants;
		bool enabled{ false };
		uint16_t port;
		std::string address;
	};
}
}
