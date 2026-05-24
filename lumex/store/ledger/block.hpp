#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/store/backend.hpp>
#include <lumex/store/block_w_sideband.hpp>
#include <lumex/store/crawler.hpp>
#include <lumex/store/typed_iterator.hpp>
#include <lumex/store/typed_iterator_templ.hpp>

#include <functional>
#include <optional>

namespace lumex::store::ledger
{
class successor_view;

class block_view
{
public:
	using iterator = lumex::store::typed_iterator<lumex::block_hash, block_w_sideband>;

public:
	block_view (lumex::store::backend &, lumex::store::ledger::successor_view &);

	void put (lumex::store::write_transaction const &, lumex::block_hash const &, lumex::block const &);
	void raw_put (lumex::store::write_transaction const &, std::vector<uint8_t> const & data, lumex::block_hash const &);
	std::shared_ptr<lumex::block> get (lumex::store::transaction const &, lumex::block_hash const &) const;
	void del (lumex::store::write_transaction const &, lumex::block_hash const &);
	bool exists (lumex::store::transaction const &, lumex::block_hash const &) const;
	uint64_t count (lumex::store::transaction const &) const;
	iterator begin (lumex::store::transaction const &, lumex::block_hash const &) const;
	iterator begin (lumex::store::transaction const &) const;
	iterator end (lumex::store::transaction const &) const;
	void for_each_par (std::function<void (lumex::store::read_transaction const &, iterator, iterator)> const & action) const;

	template <typename Transaction>
	auto crawl (Transaction & txn, lumex::block_hash const & start = { 0 }) const -> lumex::store::crawler<block_view, Transaction>
	{
		return lumex::store::crawler<block_view, Transaction>{ *this, txn, start };
	}

private:
	void block_raw_get (lumex::store::transaction const &, lumex::block_hash const &, lumex::store::db_val & value) const;

private:
	lumex::store::backend & backend;
	lumex::store::ledger::successor_view & successor_store;
};
}
