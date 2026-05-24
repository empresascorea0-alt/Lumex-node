#pragma once

#include <lumex/lib/epoch.hpp>

#include <functional>

namespace std
{
template <>
struct hash<::lumex::epoch>
{
	std::size_t operator() (::lumex::epoch const & epoch_a) const
	{
		return std::underlying_type_t<::lumex::epoch> (epoch_a);
	}
};
}
