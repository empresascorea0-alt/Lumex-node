#include <lumex/secure/parallel_traversal.hpp>
#include <lumex/store/db_val_templ.hpp>
#include <lumex/store/ledger/block.hpp>
#include <lumex/store/ledger/successor.hpp>

namespace lumex::store::ledger
{
block_view::block_view (lumex::store::backend & backend_a, lumex::store::ledger::successor_view & successor_store_a) :
	backend{ backend_a },
	successor_store{ successor_store_a }
{
}

void block_view::put (lumex::store::write_transaction const & txn, lumex::block_hash const & hash, lumex::block const & block)
{
	std::vector<uint8_t> vector;
	{
		lumex::vectorstream stream{ vector };
		lumex::serialize_block (stream, block);
		block.sideband ().serialize (stream, block.type ());
	}
	raw_put (txn, vector, hash);
	if (!block.previous ().is_zero ())
	{
		successor_store.put (txn, block.previous (), hash);
	}
	debug_assert (block.previous ().is_zero () || successor_store.get (txn, block.previous ()) == hash);
}

void block_view::raw_put (lumex::store::write_transaction const & txn, std::vector<uint8_t> const & data, lumex::block_hash const & hash)
{
	lumex::store::db_val value{ data.size (), (void *)data.data () };
	auto status = backend.put (txn, lumex::store::table::blocks, hash, value);
	backend.release_assert_success (status);
}

std::shared_ptr<lumex::block> block_view::get (lumex::store::transaction const & txn, lumex::block_hash const & hash) const
{
	lumex::store::db_val value;
	block_raw_get (txn, hash, value);
	std::shared_ptr<lumex::block> result;
	if (value.size () != 0)
	{
		lumex::bufferstream stream{ reinterpret_cast<uint8_t const *> (value.data ()), value.size () };
		lumex::block_type type;
		bool error = try_read (stream, type);
		release_assert (!error);
		result = lumex::deserialize_block (stream, type);
		release_assert (result != nullptr);
		lumex::block_sideband sideband;
		error = sideband.deserialize (stream, type);
		release_assert (!error);
		result->sideband_set (sideband);
	}
	return result;
}

void block_view::del (lumex::store::write_transaction const & txn, lumex::block_hash const & hash)
{
	auto status = backend.del (txn, lumex::store::table::blocks, hash);
	backend.release_assert_success (status);
}

bool block_view::exists (lumex::store::transaction const & txn, lumex::block_hash const & hash) const
{
	return backend.exists (txn, lumex::store::table::blocks, hash);
}

uint64_t block_view::count (lumex::store::transaction const & txn) const
{
	return backend.count (txn, lumex::store::table::blocks);
}

auto block_view::begin (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.begin (txn, lumex::store::table::blocks) };
}

auto block_view::begin (lumex::store::transaction const & txn, lumex::block_hash const & hash) const -> iterator
{
	return iterator{ backend.begin (txn, lumex::store::table::blocks, hash) };
}

auto block_view::end (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.end (txn, lumex::store::table::blocks) };
}

void block_view::for_each_par (std::function<void (lumex::store::read_transaction const &, iterator, iterator)> const & action) const
{
	parallel_traversal<lumex::uint256_t> (
	[&action, this] (lumex::uint256_t const & start, lumex::uint256_t const & end, bool const is_last) {
		auto txn = this->backend.tx_begin_read ();
		action (txn, this->begin (txn, start), !is_last ? this->begin (txn, end) : this->end (txn));
	});
}

void block_view::block_raw_get (lumex::store::transaction const & txn, lumex::block_hash const & hash, lumex::store::db_val & value) const
{
	auto status = backend.get (txn, lumex::store::table::blocks, hash, value);
	release_assert (backend.success (status) || backend.not_found (status), backend.error_string (status));
}

}
