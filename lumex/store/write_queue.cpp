#include <lumex/lib/config.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/store/write_queue.hpp>

#include <algorithm>

/*
 * write_guard
 */

lumex::store::write_guard::write_guard (write_queue & queue, writer type) :
	queue{ queue },
	type{ type }
{
	renew ();
}

lumex::store::write_guard::write_guard (write_guard && other) noexcept :
	queue{ other.queue },
	type{ other.type },
	owns{ other.owns }
{
	other.owns = false;
}

lumex::store::write_guard::~write_guard ()
{
	if (owns)
	{
		release ();
	}
}

bool lumex::store::write_guard::is_owned () const
{
	return owns;
}

void lumex::store::write_guard::release ()
{
	release_assert (owns);
	queue.release (type);
	owns = false;
}

void lumex::store::write_guard::renew ()
{
	release_assert (!owns);
	queue.acquire (type);
	owns = true;
}

/*
 * write_queue
 */

lumex::store::write_queue::write_queue ()
{
}

lumex::store::write_guard lumex::store::write_queue::wait (writer writer)
{
	return write_guard{ *this, writer };
}

bool lumex::store::write_queue::contains (writer writer) const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	return std::any_of (queue.cbegin (), queue.cend (), [writer] (auto const & item) {
		return item.first == writer;
	});
}

void lumex::store::write_queue::pop ()
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };
	if (!queue.empty ())
	{
		queue.pop_front ();
	}
	condition.notify_all ();
}

void lumex::store::write_queue::acquire (writer writer)
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };

	// There should be no duplicates in the queue (exception is testing)
	debug_assert (std::none_of (queue.cbegin (), queue.cend (), [writer] (auto const & item) {
		return item.first == writer;
	})
	|| writer == writer::testing);

	auto const id = next++;

	// Add writer to the end of the queue if it's not already waiting
	queue.push_back ({ writer, id });

	// Wait until we are at the front of the queue
	condition.wait (lock, [&] () { return queue.front ().second == id; });
}

void lumex::store::write_queue::release (writer writer)
{
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		release_assert (!queue.empty ());
		release_assert (queue.front ().first == writer);
		queue.pop_front ();
	}
	condition.notify_all ();
}
