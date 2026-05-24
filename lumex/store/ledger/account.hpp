#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/secure/account_info.hpp>
#include <lumex/store/backend.hpp>
#include <lumex/store/crawler.hpp>
#include <lumex/store/reverse_iterator.hpp>
#include <lumex/store/reverse_iterator_templ.hpp>
#include <lumex/store/typed_iterator.hpp>
#include <lumex/store/typed_iterator_templ.hpp>

#include <functional>

namespace lumex::store::ledger
{
class account_view
{
public:
	using iterator = lumex::store::typed_iterator<lumex::account, lumex::account_info>;
	using reverse_iterator = lumex::store::reverse_iterator<iterator>;

public:
	explicit account_view (lumex::store::backend &);

	void put (lumex::store::write_transaction const &, lumex::account const &, lumex::account_info const &);
	bool get (lumex::store::transaction const &, lumex::account const &, lumex::account_info &) const;
	std::optional<lumex::account_info> get (lumex::store::transaction const &, lumex::account const &) const;
	void del (lumex::store::write_transaction const &, lumex::account const &);
	bool exists (lumex::store::transaction const &, lumex::account const &) const;
	size_t count (lumex::store::transaction const &) const;
	iterator begin (lumex::store::transaction const &, lumex::account const &) const;
	iterator begin (lumex::store::transaction const &) const;
	reverse_iterator rbegin (lumex::store::transaction const &) const;
	reverse_iterator rend (lumex::store::transaction const &) const;
	iterator end (lumex::store::transaction const &) const;
	void for_each_par (std::function<void (lumex::store::read_transaction const &, iterator, iterator)> const & action) const;

	template <typename Transaction>
	auto crawl (Transaction & txn, lumex::account const & start = { 0 }) const -> lumex::store::crawler<account_view, Transaction>
	{
		return lumex::store::crawler<account_view, Transaction>{ *this, txn, start };
	}

private:
	lumex::store::backend & backend;
};
}
