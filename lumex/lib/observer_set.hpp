#pragma once

#include <lumex/lib/container_info.hpp>
#include <lumex/lib/locks.hpp>
#include <lumex/lib/utility.hpp>

#include <functional>
#include <vector>

namespace lumex
{
template <typename... T>
class observer_set final
{
public:
	using observer_type = std::function<void (T const &...)>;

public:
	void add (observer_type observer)
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		observers.push_back (observer);
	}

	void notify (T const &... args) const
	{
		// Make observers copy to allow parallel notifications
		lumex::unique_lock<lumex::mutex> lock{ mutex };
		auto observers_copy = observers;
		lock.unlock ();

		for (auto const & observer : observers_copy)
		{
			observer (args...);
		}
	}

	bool empty () const
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		return observers.empty ();
	}

	size_t size () const
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		return observers.size ();
	}

	void clear ()
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		observers.clear ();
	}

	lumex::container_info container_info () const
	{
		lumex::unique_lock<lumex::mutex> lock{ mutex };

		lumex::container_info info;
		info.put ("observers", observers);
		return info;
	}

private:
	mutable lumex::mutex mutex{ mutex_identifier (mutexes::observer_set) };
	std::vector<observer_type> observers;
};

}
