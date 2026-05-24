#pragma once

#include <lumex/lib/epoch_templ.hpp>
#include <lumex/lib/numbers.hpp>

#include <cstdint>
#include <unordered_map>

namespace lumex
{
class epoch_info
{
public:
	lumex::public_key signer;
	lumex::link link;
};
class epochs
{
public:
	/** Returns true if link matches one of the released epoch links.
	 *  WARNING: just because a legal block contains an epoch link, it does not mean it is an epoch block.
	 *  A legal block containing an epoch link can easily be constructed by sending to an address identical
	 *  to one of the epoch links.
	 *  Epoch blocks follow the following rules and a block must satisfy them all to be a true epoch block:
	 *    epoch blocks are always state blocks
	 *    epoch blocks never change the balance of an account
	 *    epoch blocks always have a link field that starts with the ascii bytes "epoch v1 block" or "epoch v2 block" (and possibly others in the future)
	 *    epoch blocks never change the representative
	 *    epoch blocks are not signed by the account key, they are signed either by genesis or by special epoch keys
	 */
	bool is_epoch_link (lumex::link const & link_a) const;
	lumex::link const & link (lumex::epoch epoch_a) const;
	lumex::public_key const & signer (lumex::epoch epoch_a) const;
	lumex::epoch epoch (lumex::link const & link_a) const;
	void add (lumex::epoch epoch_a, lumex::public_key const & signer_a, lumex::link const & link_a);
	/** Checks that new_epoch is 1 version higher than epoch */
	static bool is_sequential (lumex::epoch epoch_a, lumex::epoch new_epoch_a);

private:
	std::unordered_map<lumex::epoch, lumex::epoch_info> epochs_m;
};
}
