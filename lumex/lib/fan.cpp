#include <lumex/crypto_lib/random_pool.hpp>
#include <lumex/lib/assert.hpp>
#include <lumex/lib/fan.hpp>

namespace lumex
{
fan::fan (lumex::raw_key const & key, std::size_t count_a)
{
	auto first (std::make_unique<lumex::raw_key> (key));
	for (auto i (1); i < count_a; ++i)
	{
		auto entry (std::make_unique<lumex::raw_key> ());
		lumex::random_pool::generate_block (entry->bytes.data (), entry->bytes.size ());
		*first ^= *entry;
		values.push_back (std::move (entry));
	}
	values.push_back (std::move (first));
}

void fan::value (lumex::raw_key & prv_a) const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	value_get (prv_a);
}

void fan::value_get (lumex::raw_key & prv_a) const
{
	debug_assert (!mutex.try_lock ());
	prv_a.clear ();
	for (auto & i : values)
	{
		prv_a ^= *i;
	}
}

void fan::value_set (lumex::raw_key const & value_a)
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	lumex::raw_key value_l;
	value_get (value_l);
	*(values[0]) ^= value_l;
	*(values[0]) ^= value_a;
}
}
