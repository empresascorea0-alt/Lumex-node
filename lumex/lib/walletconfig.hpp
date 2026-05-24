#pragma once

#include <lumex/lib/errors.hpp>
#include <lumex/lib/numbers.hpp>

#include <string>

namespace lumex
{
class tomlconfig;

/** Configuration options for the Qt wallet */
class wallet_config final
{
public:
	wallet_config ();
	/** Update this instance by parsing the given wallet and account */
	lumex::error parse (std::string const & wallet_a, std::string const & account_a);
	lumex::error serialize_toml (lumex::tomlconfig & toml_a) const;
	lumex::error deserialize_toml (lumex::tomlconfig & toml_a);
	lumex::wallet_id wallet;
	lumex::account account{};
};
}
