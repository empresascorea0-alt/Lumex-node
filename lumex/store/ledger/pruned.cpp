#include <lumex/crypto_lib/random_pool.hpp>
#include <lumex/secure/parallel_traversal.hpp>
#include <lumex/store/ledger/pruned.hpp>

namespace lumex::store::ledger
{
pruned_view::pruned_view (lumex::store::backend & backend_a) :
	backend{ backend_a }
{
}

void pruned_view::put (lumex::store::write_transaction const & txn, lumex::block_hash const & hash)
{
	auto status = backend.put (txn, lumex::store::table::pruned, hash, nullptr);
	backend.release_assert_success (status);
}

void pruned_view::del (lumex::store::write_transaction const & txn, lumex::block_hash const & hash)
{
	auto status = backend.del (txn, lumex::store::table::pruned, hash);
	backend.release_assert_success (status);
}

bool pruned_view::exists (lumex::store::transaction const & txn, lumex::block_hash const & hash) const
{
	return backend.exists (txn, lumex::store::table::pruned, hash);
}

lumex::block_hash pruned_view::random (lumex::store::transaction const & txn) const
{
	lumex::block_hash random_hash;
	lumex::random_pool::generate_block (random_hash.bytes.data (), random_hash.bytes.size ());
	auto existing = begin (txn, random_hash);
	if (existing == end (txn))
	{
		existing = begin (txn);
	}
	return existing != end (txn) ? existing->first : 0;
}

size_t pruned_view::count (lumex::store::transaction const & txn) const
{
	return backend.count (txn, lumex::store::table::pruned);
}

void pruned_view::clear ()
{
	auto status = backend.clear (lumex::store::table::pruned);
	backend.release_assert_success (status);
}

auto pruned_view::begin (lumex::store::transaction const & txn, lumex::block_hash const & hash) const -> iterator
{
	return iterator{ backend.begin (txn, lumex::store::table::pruned, hash) };
}

auto pruned_view::begin (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.begin (txn, lumex::store::table::pruned) };
}

auto pruned_view::end (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.end (txn, lumex::store::table::pruned) };
}

void pruned_view::for_each_par (std::function<void (lumex::store::read_transaction const &, iterator, iterator)> const & action) const
{
	parallel_traversal<lumex::uint256_t> (
	[&action, this] (lumex::uint256_t const & start, lumex::uint256_t const & end, bool const is_last) {
		auto txn = this->backend.tx_begin_read ();
		action (txn, this->begin (txn, start), !is_last ? this->begin (txn, end) : this->end (txn));
	});
}
}
