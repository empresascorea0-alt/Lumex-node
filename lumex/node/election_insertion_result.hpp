#pragma once

#include <memory>

namespace lumex
{
class election;
class election_insertion_result final
{
public:
	std::shared_ptr<lumex::election> election;
	bool inserted{ false };
};
}
