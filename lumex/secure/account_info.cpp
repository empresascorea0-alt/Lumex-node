#include <lumex/lib/stream.hpp>
#include <lumex/secure/account_info.hpp>

lumex::account_info::account_info (lumex::block_hash const & head_a, lumex::account const & representative_a, lumex::block_hash const & open_block_a, lumex::amount const & balance_a, lumex::seconds_t modified_a, uint64_t block_count_a, lumex::epoch epoch_a) :
	head (head_a),
	representative (representative_a),
	open_block (open_block_a),
	balance (balance_a),
	modified (modified_a),
	block_count (block_count_a),
	epoch_m (epoch_a)
{
}

bool lumex::account_info::deserialize (lumex::stream & stream_a)
{
	auto error (false);
	try
	{
		lumex::read (stream_a, head.bytes);
		lumex::read (stream_a, representative.bytes);
		lumex::read (stream_a, open_block.bytes);
		lumex::read (stream_a, balance.bytes);
		lumex::read (stream_a, modified);
		lumex::read (stream_a, block_count);
		lumex::read (stream_a, epoch_m);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool lumex::account_info::operator== (lumex::account_info const & other_a) const
{
	return head == other_a.head && representative == other_a.representative && open_block == other_a.open_block && balance == other_a.balance && modified == other_a.modified && block_count == other_a.block_count && epoch () == other_a.epoch ();
}

bool lumex::account_info::operator!= (lumex::account_info const & other_a) const
{
	return !(*this == other_a);
}

size_t lumex::account_info::db_size () const
{
	debug_assert (reinterpret_cast<uint8_t const *> (this) == reinterpret_cast<uint8_t const *> (&head));
	debug_assert (reinterpret_cast<uint8_t const *> (&head) + sizeof (head) == reinterpret_cast<uint8_t const *> (&representative));
	debug_assert (reinterpret_cast<uint8_t const *> (&representative) + sizeof (representative) == reinterpret_cast<uint8_t const *> (&open_block));
	debug_assert (reinterpret_cast<uint8_t const *> (&open_block) + sizeof (open_block) == reinterpret_cast<uint8_t const *> (&balance));
	debug_assert (reinterpret_cast<uint8_t const *> (&balance) + sizeof (balance) == reinterpret_cast<uint8_t const *> (&modified));
	debug_assert (reinterpret_cast<uint8_t const *> (&modified) + sizeof (modified) == reinterpret_cast<uint8_t const *> (&block_count));
	debug_assert (reinterpret_cast<uint8_t const *> (&block_count) + sizeof (block_count) == reinterpret_cast<uint8_t const *> (&epoch_m));
	return sizeof (head) + sizeof (representative) + sizeof (open_block) + sizeof (balance) + sizeof (modified) + sizeof (block_count) + sizeof (epoch_m);
}

lumex::epoch lumex::account_info::epoch () const
{
	return epoch_m;
}

size_t lumex::account_info_v22::db_size () const
{
	debug_assert (reinterpret_cast<uint8_t const *> (this) == reinterpret_cast<uint8_t const *> (&head));
	debug_assert (reinterpret_cast<uint8_t const *> (&head) + sizeof (head) == reinterpret_cast<uint8_t const *> (&representative));
	debug_assert (reinterpret_cast<uint8_t const *> (&representative) + sizeof (representative) == reinterpret_cast<uint8_t const *> (&open_block));
	debug_assert (reinterpret_cast<uint8_t const *> (&open_block) + sizeof (open_block) == reinterpret_cast<uint8_t const *> (&balance));
	debug_assert (reinterpret_cast<uint8_t const *> (&balance) + sizeof (balance) == reinterpret_cast<uint8_t const *> (&modified));
	debug_assert (reinterpret_cast<uint8_t const *> (&modified) + sizeof (modified) == reinterpret_cast<uint8_t const *> (&block_count));
	debug_assert (reinterpret_cast<uint8_t const *> (&block_count) + sizeof (block_count) == reinterpret_cast<uint8_t const *> (&epoch_m));
	return sizeof (head) + sizeof (representative) + sizeof (open_block) + sizeof (balance) + sizeof (modified) + sizeof (block_count) + sizeof (epoch_m);
}

bool lumex::account_info_v22::deserialize (lumex::stream & stream_a)
{
	auto error (false);
	try
	{
		lumex::read (stream_a, head.bytes);
		lumex::read (stream_a, representative.bytes);
		lumex::read (stream_a, open_block.bytes);
		lumex::read (stream_a, balance.bytes);
		lumex::read (stream_a, modified);
		lumex::read (stream_a, block_count);
		lumex::read (stream_a, epoch_m);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}
