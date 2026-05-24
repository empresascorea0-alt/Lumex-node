#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/store/backend.hpp>
#include <lumex/store/typed_iterator.hpp>
#include <lumex/store/typed_iterator_templ.hpp>

#include <functional>
#include <optional>

namespace lumex::store::ledger
{
class successor_view
{
public:
	using iterator = store::typed_iterator<lumex::block_hash, lumex::block_hash>;

public:
	explicit successor_view (lumex::store::backend &);

	void put (lumex::store::write_transaction const &, lumex::block_hash const &, lumex::block_hash const & successor);
	void del (lumex::store::write_transaction const &, lumex::block_hash const &);
	std::optional<lumex::block_hash> get (lumex::store::transaction const &, lumex::block_hash const &) const;
	bool exists (lumex::store::transaction const &, lumex::block_hash const &) const;
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
