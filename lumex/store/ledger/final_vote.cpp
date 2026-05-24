#include <lumex/secure/parallel_traversal.hpp>
#include <lumex/store/ledger/final_vote.hpp>

namespace lumex::store::ledger
{
final_vote_view::final_vote_view (lumex::store::backend & backend_a) :
	backend{ backend_a }
{
}

bool final_vote_view::put (lumex::store::write_transaction const & txn, lumex::qualified_root const & root, lumex::block_hash const & hash)
{
	lumex::store::db_val value;
	auto status = backend.get (txn, lumex::store::table::final_votes, root, value);
	release_assert (backend.success (status) || backend.not_found (status), backend.error_string (status));
	bool result = true;
	if (backend.success (status))
	{
		result = static_cast<lumex::block_hash> (value) == hash;
	}
	else
	{
		status = backend.put (txn, lumex::store::table::final_votes, root, hash);
		backend.release_assert_success (status);
	}
	return result;
}

std::optional<lumex::block_hash> final_vote_view::get (lumex::store::transaction const & txn, lumex::qualified_root const & qualified_root) const
{
	lumex::store::db_val result;
	auto status = backend.get (txn, lumex::store::table::final_votes, qualified_root, result);
	std::optional<lumex::block_hash> final_vote_hash;
	if (backend.success (status))
	{
		final_vote_hash = static_cast<lumex::block_hash> (result);
	}
	return final_vote_hash;
}

void final_vote_view::del (lumex::store::write_transaction const & txn, lumex::qualified_root const & root)
{
	auto status = backend.del (txn, lumex::store::table::final_votes, root);
	backend.release_assert_success (status);
}

size_t final_vote_view::count (lumex::store::transaction const & txn) const
{
	return backend.count (txn, lumex::store::table::final_votes);
}

bool final_vote_view::empty (lumex::store::transaction const & txn) const
{
	return backend.empty (txn, lumex::store::table::final_votes);
}

void final_vote_view::clear ()
{
	auto status = backend.clear (lumex::store::table::final_votes);
	backend.release_assert_success (status);
}

auto final_vote_view::begin (lumex::store::transaction const & txn, lumex::qualified_root const & root) const -> iterator
{
	return iterator{ backend.begin (txn, lumex::store::table::final_votes, root) };
}

auto final_vote_view::begin (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.begin (txn, lumex::store::table::final_votes) };
}

auto final_vote_view::end (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.end (txn, lumex::store::table::final_votes) };
}

void final_vote_view::for_each_par (std::function<void (lumex::store::read_transaction const &, iterator, iterator)> const & action) const
{
	parallel_traversal<lumex::uint512_t> (
	[&action, this] (lumex::uint512_t const & start, lumex::uint512_t const & end, bool const is_last) {
		auto txn = this->backend.tx_begin_read ();
		action (txn, this->begin (txn, start), !is_last ? this->begin (txn, end) : this->end (txn));
	});
}
}
