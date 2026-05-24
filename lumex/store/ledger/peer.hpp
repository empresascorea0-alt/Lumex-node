#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/secure/endpoint_key.hpp>
#include <lumex/store/backend.hpp>
#include <lumex/store/typed_iterator.hpp>
#include <lumex/store/typed_iterator_templ.hpp>

namespace lumex::store::ledger
{
class peer_view
{
public:
	using iterator = store::typed_iterator<lumex::endpoint_key, lumex::millis_t>;

public:
	explicit peer_view (lumex::store::backend &);

	void put (lumex::store::write_transaction const &, lumex::endpoint_key const &, lumex::millis_t timestamp);
	lumex::millis_t get (lumex::store::transaction const &, lumex::endpoint_key const &) const;
	void del (lumex::store::write_transaction const &, lumex::endpoint_key const &);
	bool exists (lumex::store::transaction const &, lumex::endpoint_key const &) const;
	size_t count (lumex::store::transaction const &) const;
	void clear ();
	iterator begin (lumex::store::transaction const &) const;
	iterator end (lumex::store::transaction const &) const;

private:
	lumex::store::backend & backend;
};
}
