#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/store/backend.hpp>
#include <lumex/store/typed_iterator.hpp>
#include <lumex/store/typed_iterator_templ.hpp>

#include <functional>

namespace lumex::store::ledger
{
class confirmation_height_view
{
public:
	using iterator = store::typed_iterator<lumex::account, lumex::confirmation_height_info>;

public:
	explicit confirmation_height_view (lumex::store::backend &);

	void put (lumex::store::write_transaction const &, lumex::account const &, lumex::confirmation_height_info const &);
	bool get (lumex::store::transaction const &, lumex::account const &, lumex::confirmation_height_info &) const;
	std::optional<lumex::confirmation_height_info> get (lumex::store::transaction const &, lumex::account const &);
	bool exists (lumex::store::transaction const &, lumex::account const &) const;
	void del (lumex::store::write_transaction const &, lumex::account const &);
	uint64_t count (lumex::store::transaction const &) const;
	bool empty (lumex::store::transaction const &) const;
	void clear ();
	iterator begin (lumex::store::transaction const &, lumex::account const &) const;
	iterator begin (lumex::store::transaction const &) const;
	iterator end (lumex::store::transaction const &) const;
	void for_each_par (std::function<void (lumex::store::read_transaction const &, iterator, iterator)> const & action) const;

private:
	lumex::store::backend & backend;
};
}
