#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/store/fwd.hpp>
#include <lumex/store/typed_iterator.hpp>
#include <lumex/store/typed_iterator_templ.hpp>

#include <optional>

namespace lumex::store::ledger
{
class topology_view
{
public:
	using iterator = lumex::store::typed_iterator<lumex::topo_key, std::nullptr_t>;

	explicit topology_view (lumex::store::backend &);

	void put (lumex::store::write_transaction const &, lumex::topo_key const & key);
	void del (lumex::store::write_transaction const &, lumex::topo_key const & key);
	bool exists (lumex::store::transaction const &, lumex::topo_key const & key) const;
	std::optional<uint64_t> latest (lumex::store::transaction const &) const;
	uint64_t count (lumex::store::transaction const &) const;
	void clear ();

	iterator begin (lumex::store::transaction const &, lumex::topo_key const &) const;
	iterator begin (lumex::store::transaction const &, uint64_t topo_height) const;
	iterator begin (lumex::store::transaction const &) const;
	iterator end (lumex::store::transaction const &) const;

private:
	lumex::store::backend & backend;
};
}
