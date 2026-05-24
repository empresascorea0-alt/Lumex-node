#pragma once

#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>

#include <cstddef>
#include <memory>
#include <vector>

namespace lumex
{
class fan final
{
public:
	fan (lumex::raw_key const & key, std::size_t count);
	void value (lumex::raw_key & result) const;
	void value_set (lumex::raw_key const & value);
	std::vector<std::unique_ptr<lumex::raw_key>> values;

private:
	mutable lumex::mutex mutex;
	void value_get (lumex::raw_key & result) const;
};
}
