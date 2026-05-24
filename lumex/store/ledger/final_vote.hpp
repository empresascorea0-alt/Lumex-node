#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/store/backend.hpp>
#include <lumex/store/typed_iterator.hpp>
#include <lumex/store/typed_iterator_templ.hpp>

#include <functional>

namespace lumex::store::ledger
{
class final_vote_view
{
public:
	using iterator = store::typed_iterator<lumex::qualified_root, lumex::block_hash>;

public:
	explicit final_vote_view (lumex::store::backend &);

	bool put (lumex::store::write_transaction const &, lumex::qualified_root const &, lumex::block_hash const &);
	std::optional<lumex::block_hash> get (lumex::store::transaction const &, lumex::qualified_root const &) const;
	void del (lumex::store::write_transaction const &, lumex::qualified_root const &);
	size_t count (lumex::store::transaction const &) const;
	bool empty (lumex::store::transaction const &) const;
	void clear ();
	iterator begin (lumex::store::transaction const &, lumex::qualified_root const &) const;
	iterator begin (lumex::store::transaction const &) const;
	iterator end (lumex::store::transaction const &) const;
	void for_each_par (std::function<void (lumex::store::read_transaction const &, iterator, iterator)> const & action) const;

private:
	lumex::store::backend & backend;
};
}
