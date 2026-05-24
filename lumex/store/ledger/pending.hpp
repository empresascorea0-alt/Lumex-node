#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/secure/pending_info.hpp>
#include <lumex/store/backend.hpp>
#include <lumex/store/crawler.hpp>
#include <lumex/store/typed_iterator.hpp>
#include <lumex/store/typed_iterator_templ.hpp>

#include <functional>

namespace lumex::store::ledger
{
class pending_view
{
public:
	using iterator = lumex::store::typed_iterator<lumex::pending_key, lumex::pending_info>;

public:
	explicit pending_view (lumex::store::backend &);

	void put (lumex::store::write_transaction const &, lumex::pending_key const &, lumex::pending_info const &);
	void del (lumex::store::write_transaction const &, lumex::pending_key const &);
	std::optional<lumex::pending_info> get (lumex::store::transaction const &, lumex::pending_key const &) const;
	bool exists (lumex::store::transaction const &, lumex::pending_key const &) const;
	bool any (lumex::store::transaction const &, lumex::account const &) const;
	iterator begin (lumex::store::transaction const &, lumex::pending_key const &) const;
	iterator begin (lumex::store::transaction const &) const;
	iterator end (lumex::store::transaction const &) const;
	void for_each_par (std::function<void (lumex::store::read_transaction const &, iterator, iterator)> const & action) const;

	template <typename Transaction>
	auto crawl (Transaction & txn, lumex::account const & start = { 0 }) const -> lumex::store::crawler<pending_view, Transaction>
	{
		return lumex::store::crawler<pending_view, Transaction>{ *this, txn, start };
	}

private:
	lumex::store::backend & backend;
};
}

/**
 * Specialization for pending table which has a compound key (account + hash).
 * Groups entries by account, so seek_key_type is lumex::account.
 */
template <>
struct lumex::store::crawler_traits<lumex::pending_key, lumex::pending_info> : lumex::store::crawler_traits<lumex::account, lumex::pending_info>
{
	static lumex::pending_key make_iterator_key (seek_key_type const & account)
	{
		return lumex::pending_key{ account, 0 };
	}

	static seek_key_type group_key (lumex::pending_key const & key)
	{
		return key.account;
	}
};