#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/store/backend.hpp>
#include <lumex/store/reverse_iterator.hpp>
#include <lumex/store/reverse_iterator_templ.hpp>
#include <lumex/store/typed_iterator.hpp>
#include <lumex/store/typed_iterator_templ.hpp>

namespace lumex::store::ledger
{
class online_weight_view
{
public:
	using iterator = store::typed_iterator<uint64_t, lumex::amount>;
	using reverse_iterator = store::reverse_iterator<iterator>;

public:
	explicit online_weight_view (lumex::store::backend &);

	void put (lumex::store::write_transaction const &, uint64_t time, lumex::amount const &);
	void del (lumex::store::write_transaction const &, uint64_t time);
	iterator begin (lumex::store::transaction const &) const;
	reverse_iterator rbegin (lumex::store::transaction const &) const;
	reverse_iterator rend (lumex::store::transaction const &) const;
	iterator end (lumex::store::transaction const &) const;
	size_t count (lumex::store::transaction const &) const;
	void clear ();

private:
	lumex::store::backend & backend;
};
}
