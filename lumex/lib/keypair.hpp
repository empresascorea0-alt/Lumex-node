#pragma once

#include <lumex/lib/numbers.hpp>

namespace lumex
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
	keypair (lumex::raw_key &&);

	/// Create a keypair given a hex string of the private key
	explicit keypair (std::string const &);

public:
	lumex::public_key pub;
	lumex::raw_key prv;
};
}
