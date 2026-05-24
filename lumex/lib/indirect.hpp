#pragma once

#include <memory>
#include <type_traits>

namespace lumex
{
/**
 * Value-semantic wrapper around a heap-allocated T.
 * Provides deep copy, move, and implicit conversion to T& / T const&.
 * Similar to std::indirect (C++26).
 * Use when you need value semantics but only a forward declaration of T in the header.
 */
template <typename T>
class indirect
{
public:
	indirect () :
		ptr{ std::make_unique<T> () }
	{
	}

	template <typename Arg, typename... Rest>
		requires (!std::same_as<std::remove_cvref_t<Arg>, indirect>)
	indirect (Arg && arg, Rest &&... rest) :
		ptr{ std::make_unique<T> (std::forward<Arg> (arg), std::forward<Rest> (rest)...) }
	{
	}

	indirect (indirect const & other) :
		ptr{ std::make_unique<T> (*other.ptr) }
	{
	}
	indirect (indirect &&) noexcept = default;
	indirect & operator= (indirect const & other)
	{
		if (this != &other)
			*ptr = *other.ptr;
		return *this;
	}
	indirect & operator= (indirect &&) noexcept = default;
	~indirect () = default;

	T * operator->()
	{
		return ptr.get ();
	}
	T const * operator->() const
	{
		return ptr.get ();
	}
	T & operator* ()
	{
		return *ptr;
	}
	T const & operator* () const
	{
		return *ptr;
	}

	operator T & ()
	{
		return *ptr;
	}
	operator T const & () const
	{
		return *ptr;
	}

private:
	std::unique_ptr<T> ptr;
};
}
