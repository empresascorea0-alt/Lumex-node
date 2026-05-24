#include <lumex/lib/assert.hpp>
#include <lumex/lib/kdf.hpp>

#include <argon2.h>

namespace lumex
{
void kdf::phs (lumex::raw_key & result_a, std::string const & password_a, lumex::uint256_union const & salt_a)
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	auto success (argon2_hash (1, kdf_work, 1, password_a.data (), password_a.size (), salt_a.bytes.data (), salt_a.bytes.size (), result_a.bytes.data (), result_a.bytes.size (), NULL, 0, Argon2_d, 0x10));
	release_assert (success == 0);
	(void)success;
}
}
