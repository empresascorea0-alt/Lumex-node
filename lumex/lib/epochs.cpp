#include <lumex/lib/epochs.hpp>
#include <lumex/lib/utility.hpp>

#include <algorithm>

lumex::link const & lumex::epochs::link (lumex::epoch epoch_a) const
{
	return epochs_m.at (epoch_a).link;
}

bool lumex::epochs::is_epoch_link (lumex::link const & link_a) const
{
	return std::any_of (epochs_m.begin (), epochs_m.end (), [&link_a] (auto const & item_a) { return item_a.second.link == link_a; });
}

lumex::public_key const & lumex::epochs::signer (lumex::epoch epoch_a) const
{
	return epochs_m.at (epoch_a).signer;
}

lumex::epoch lumex::epochs::epoch (lumex::link const & link_a) const
{
	auto existing (std::find_if (epochs_m.begin (), epochs_m.end (), [&link_a] (auto const & item_a) { return item_a.second.link == link_a; }));
	debug_assert (existing != epochs_m.end ());
	return existing->first;
}

void lumex::epochs::add (lumex::epoch epoch_a, lumex::public_key const & signer_a, lumex::link const & link_a)
{
	debug_assert (epochs_m.find (epoch_a) == epochs_m.end ());
	epochs_m[epoch_a] = { signer_a, link_a };
}

bool lumex::epochs::is_sequential (lumex::epoch epoch_a, lumex::epoch new_epoch_a)
{
	auto head_epoch = std::underlying_type_t<lumex::epoch> (epoch_a);
	bool is_valid_epoch (head_epoch >= std::underlying_type_t<lumex::epoch> (lumex::epoch::epoch_0));
	return is_valid_epoch && (std::underlying_type_t<lumex::epoch> (new_epoch_a) == (head_epoch + 1));
}
