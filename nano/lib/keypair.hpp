#pragma once

#include <nano/lib/numbers.hpp>

namespace nano
{
/**
 * A key pair. The private key is generated from the random pool, or passed in
 * as a hex string. The public key is derived using ed25519.
 */
class keypair
{
public:
	/// Create a new random keypair
	keypair ();

	/// Create a keypair given a private key
	keypair (nano::raw_key &&);

	/// Create a keypair given a hex string of the private key
	explicit keypair (std::string const &);

public:
	nano::public_key pub;
	nano::raw_key prv;
};
}
