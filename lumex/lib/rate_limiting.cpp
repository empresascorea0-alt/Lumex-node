#include <lumex/lib/locks.hpp>
#include <lumex/lib/rate_limiting.hpp>
#include <lumex/lib/utility.hpp>

#include <limits>

/*
 * token_bucket
 */

lumex::rate::token_bucket::token_bucket (std::size_t max_token_count_a, std::size_t refill_rate_a)
{
	reset (max_token_count_a, refill_rate_a);
}

bool lumex::rate::token_bucket::try_consume (unsigned tokens_required_a)
{
	debug_assert (tokens_required_a <= 1e9);
	refill ();
	bool possible = current_size >= tokens_required_a;
	if (possible)
	{
		current_size -= tokens_required_a;
	}
	else if (tokens_required_a == 1e9)
	{
		current_size = 0;
	}

	// Keep track of smallest observed bucket size so burst size can be computed (for tests and stats)
	smallest_size = std::min (smallest_size, current_size);

	return possible || refill_rate == unlimited_rate_sentinel;
}

void lumex::rate::token_bucket::refill ()
{
	auto now (std::chrono::steady_clock::now ());
	std::size_t tokens_to_add = static_cast<std::size_t> (std::chrono::duration_cast<std::chrono::lumexseconds> (now - last_refill).count () / 1e9 * refill_rate);
	// Only update if there are any tokens to add
	if (tokens_to_add > 0)
	{
		current_size = std::min (current_size + tokens_to_add, max_token_count);
		last_refill = std::chrono::steady_clock::now ();
	}
}

void lumex::rate::token_bucket::reset (std::size_t max_token_count_a, std::size_t refill_rate_a)
{
	// A token count of 0 indicates unlimited capacity. We use 1e9 as
	// a sentinel, allowing largest burst to still be computed.
	if (max_token_count_a == 0 || refill_rate_a == 0)
	{
		refill_rate_a = max_token_count_a = unlimited_rate_sentinel;
	}
	max_token_count = smallest_size = current_size = max_token_count_a;
	refill_rate = refill_rate_a;
	last_refill = std::chrono::steady_clock::now ();
}

std::size_t lumex::rate::token_bucket::largest_burst () const
{
	return max_token_count - smallest_size;
}

std::size_t lumex::rate::token_bucket::size () const
{
	return current_size;
}

/*
 * rate_limiter
 */

lumex::rate_limiter::rate_limiter (std::size_t limit_a, double burst_ratio_a) :
	bucket (static_cast<std::size_t> (limit_a * burst_ratio_a), limit_a),
	limit{ limit_a },
	burst_ratio{ burst_ratio_a }
{
}

bool lumex::rate_limiter::should_pass (std::size_t message_size_a)
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return bucket.try_consume (lumex::narrow_cast<unsigned int> (message_size_a));
}

void lumex::rate_limiter::reset (std::size_t limit_a, double burst_ratio_a)
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	bucket.reset (static_cast<std::size_t> (limit_a * burst_ratio_a), limit_a);
	limit = limit_a;
	burst_ratio = burst_ratio_a;
}

std::size_t lumex::rate_limiter::size () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return bucket.size ();
}

std::pair<std::size_t, double> lumex::rate_limiter::get_limit () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return { limit, burst_ratio };
}