#include <lumex/lib/utility.hpp>
#include <lumex/node/bootstrap/throttle.hpp>

lumex::bootstrap::throttle::throttle (std::size_t size) :
	successes_m{ size }
{
	samples.insert (samples.end (), size, true);
	debug_assert (size > 0);
}

void lumex::bootstrap::throttle::reset ()
{
	successes_m = samples.size ();
	std::fill (samples.begin (), samples.end (), true);
}

bool lumex::bootstrap::throttle::throttled () const
{
	return successes_m == 0;
}

void lumex::bootstrap::throttle::add (bool sample)
{
	debug_assert (!samples.empty ());
	pop ();
	samples.push_back (sample);
	if (sample)
	{
		++successes_m;
	}
}

void lumex::bootstrap::throttle::resize (std::size_t size)
{
	debug_assert (size > 0);
	while (size < samples.size ())
	{
		pop ();
	}
	while (size > samples.size ())
	{
		samples.push_back (false);
	}
}

std::size_t lumex::bootstrap::throttle::size () const
{
	return samples.size ();
}

std::size_t lumex::bootstrap::throttle::successes () const
{
	return successes_m;
}

void lumex::bootstrap::throttle::pop ()
{
	if (samples.front ())
	{
		--successes_m;
	}
	samples.pop_front ();
}
