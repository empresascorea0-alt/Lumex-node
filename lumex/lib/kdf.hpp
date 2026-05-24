#pragma once

#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>

#include <string>

namespace lumex
{
class kdf final
{
public:
	kdf (unsigned const & kdf_work) :
		kdf_work{ kdf_work }
	{
	}

	void phs (lumex::raw_key & result, std::string const & password, lumex::uint256_union const & salt);

	lumex::mutex mutex;
	unsigned const & kdf_work;
};
}
