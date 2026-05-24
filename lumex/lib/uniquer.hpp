#pragma once

#include <lumex/lib/container_info.hpp>
#include <lumex/lib/interval.hpp>
#include <lumex/lib/locks.hpp>
#include <lumex/lib/utility.hpp>

#include <memory>
#include <unordered_map>

namespace lumex
{
template <typename Key, typename Value>
class uniquer final
{
public:
	using key_type = Key;
	using value_type = Value;

	std::shared_ptr<Value> unique (std::shared_ptr<Value> const & value)
	{
		if (value == nullptr)
		{
			return nullptr;
		}

		// Types used as value need to provide full_hash()
		Key hash = value->full_hash ();

		lumex::lock_guard<lumex::mutex> guard{ mutex };

		if (cleanup_interval.elapse (cleanup_cutoff))
		{
			cleanup ();
		}

		auto & existing = values[hash];
		if (auto result = existing.lock ())
		{
			return result;
		}
		else
		{
			existing = value;
		}

		return value;
	}

	std::size_t size () const
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		return values.size ();
	}

	lumex::container_info container_info () const
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };

		lumex::container_info info;
		info.put ("cache", values);
		return info;
	}

	static std::chrono::milliseconds constexpr cleanup_cutoff{ 500 };

private:
	void cleanup ()
	{
		debug_assert (!mutex.try_lock ());

		std::erase_if (values, [] (auto const & item) {
			return item.second.expired ();
		});
	}

private:
	mutable lumex::mutex mutex;
	std::unordered_map<Key, std::weak_ptr<Value>> values;
	lumex::interval cleanup_interval;
};
}