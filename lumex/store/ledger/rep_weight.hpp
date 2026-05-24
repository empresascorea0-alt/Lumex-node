#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/store/backend.hpp>
#include <lumex/store/typed_iterator.hpp>
#include <lumex/store/typed_iterator_templ.hpp>

#include <functional>

namespace lumex::store::ledger
{
class rep_weight_view
{
public:
	using iterator = store::typed_iterator<lumex::account, lumex::uint128_union>;

public:
	explicit rep_weight_view (lumex::store::backend &);

	uint64_t count (lumex::store::transaction const &) const;
	lumex::uint128_t get (lumex::store::transaction const &, lumex::account const &) const;
	void put (lumex::store::write_transaction const &, lumex::account const &, lumex::uint128_t const &);
	void del (lumex::store::write_transaction const &, lumex::account const &);
	iterator begin (lumex::store::transaction const &, lumex::account const &) const;
	iterator begin (lumex::store::transaction const &) const;
	iterator end (lumex::store::transaction const &) const;
	void for_each_par (std::function<void (lumex::store::read_transaction const &, iterator, iterator)> const & action) const;

private:
	lumex::store::backend & backend;
};
}
