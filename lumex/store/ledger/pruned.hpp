#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/store/backend.hpp>
#include <lumex/store/typed_iterator.hpp>
#include <lumex/store/typed_iterator_templ.hpp>

#include <functional>

namespace lumex::store::ledger
{
class pruned_view
{
public:
	using iterator = store::typed_iterator<lumex::block_hash, std::nullptr_t>;

public:
	explicit pruned_view (lumex::store::backend &);

	void put (lumex::store::write_transaction const &, lumex::block_hash const &);
	void del (lumex::store::write_transaction const &, lumex::block_hash const &);
	bool exists (lumex::store::transaction const &, lumex::block_hash const &) const;
	lumex::block_hash random (lumex::store::transaction const &) const;
	size_t count (lumex::store::transaction const &) const;
	void clear ();
	iterator begin (lumex::store::transaction const &, lumex::block_hash const &) const;
	iterator begin (lumex::store::transaction const &) const;
	iterator end (lumex::store::transaction const &) const;
	void for_each_par (std::function<void (lumex::store::read_transaction const &, iterator, iterator)> const & action) const;

private:
	lumex::store::backend & backend;
};
}
