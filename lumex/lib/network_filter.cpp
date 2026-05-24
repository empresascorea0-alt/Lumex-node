#include <lumex/crypto_lib/random_pool.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/locks.hpp>
#include <lumex/lib/network_filter.hpp>
#include <lumex/lib/stream.hpp>
#include <lumex/secure/common.hpp>

lumex::network_filter::network_filter (size_t size_a, epoch_t age_cutoff_a) :
	items (size_a, { 0 }),
	age_cutoff{ age_cutoff_a }
{
	lumex::random_pool::generate_block (key, key.size ());
}

void lumex::network_filter::update (epoch_t epoch_inc)
{
	debug_assert (epoch_inc > 0);
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	current_epoch += epoch_inc;
}

bool lumex::network_filter::compare (entry const & existing, digest_t const & digest) const
{
	debug_assert (!mutex.try_lock ());
	// Only consider digests to be the same if the epoch is within the age cutoff
	return existing.digest == digest && existing.epoch + age_cutoff >= current_epoch;
}

bool lumex::network_filter::apply (uint8_t const * bytes_a, size_t count_a, lumex::uint128_t * digest_out)
{
	// Get hash before locking
	auto digest = hash (bytes_a, count_a);
	if (digest_out)
	{
		*digest_out = digest;
	}
	return apply (digest);
}

bool lumex::network_filter::apply (digest_t const & digest)
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };

	auto & element = get_element (digest);
	bool existed = compare (element, digest);
	if (!existed)
	{
		// Replace likely old element with a new one
		element = { digest, current_epoch };
	}
	return existed;
}

bool lumex::network_filter::check (uint8_t const * bytes, size_t count) const
{
	return check (hash (bytes, count));
}

bool lumex::network_filter::check (digest_t const & digest) const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	auto & element = get_element (digest);
	return compare (element, digest);
}

void lumex::network_filter::clear (digest_t const & digest)
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	auto & element = get_element (digest);
	if (compare (element, digest))
	{
		element = { 0 };
	}
}

void lumex::network_filter::clear (std::vector<digest_t> const & digests)
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	for (auto const & digest : digests)
	{
		auto & element = get_element (digest);
		if (compare (element, digest))
		{
			element = { 0 };
		}
	}
}

void lumex::network_filter::clear (uint8_t const * bytes_a, size_t count_a)
{
	clear (hash (bytes_a, count_a));
}

template <typename OBJECT>
void lumex::network_filter::clear (OBJECT const & object_a)
{
	clear (hash (object_a));
}

void lumex::network_filter::clear ()
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	items.assign (items.size (), { 0 });
}

template <typename OBJECT>
lumex::uint128_t lumex::network_filter::hash (OBJECT const & object_a) const
{
	std::vector<uint8_t> bytes;
	{
		lumex::vectorstream stream (bytes);
		object_a->serialize (stream);
	}
	return hash (bytes.data (), bytes.size ());
}

auto lumex::network_filter::get_element (lumex::uint128_t const & hash_a) -> entry &
{
	debug_assert (!mutex.try_lock ());
	debug_assert (items.size () > 0);
	size_t index (hash_a % items.size ());
	return items[index];
}

auto lumex::network_filter::get_element (lumex::uint128_t const & hash_a) const -> entry const &
{
	debug_assert (!mutex.try_lock ());
	debug_assert (items.size () > 0);
	size_t index (hash_a % items.size ());
	return items[index];
}

lumex::uint128_t lumex::network_filter::hash (uint8_t const * bytes_a, size_t count_a) const
{
	lumex::uint128_union digest{ 0 };
	siphash_t siphash (key, static_cast<unsigned int> (key.size ()));
	siphash.CalculateDigest (digest.bytes.data (), bytes_a, count_a);
	return digest.number ();
}

// Explicitly instantiate
template lumex::uint128_t lumex::network_filter::hash (std::shared_ptr<lumex::block> const &) const;
template void lumex::network_filter::clear (std::shared_ptr<lumex::block> const &);
