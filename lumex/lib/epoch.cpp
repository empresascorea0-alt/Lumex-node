#include <lumex/lib/epoch.hpp>
#include <lumex/lib/utility.hpp>

std::underlying_type_t<lumex::epoch> lumex::normalized_epoch (lumex::epoch epoch_a)
{
	// Currently assumes that the epoch versions in the enum are sequential.
	auto start = std::underlying_type_t<lumex::epoch> (lumex::epoch::epoch_0);
	auto end = std::underlying_type_t<lumex::epoch> (epoch_a);
	debug_assert (end >= start);
	return end - start;
}
