#include <lumex/lib/stream.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/pending_info.hpp>

lumex::pending_info::pending_info (lumex::account const & source_a, lumex::amount const & amount_a, lumex::epoch epoch_a) :
	source (source_a),
	amount (amount_a),
	epoch (epoch_a)
{
}

bool lumex::pending_info::deserialize (lumex::stream & stream_a)
{
	auto error (false);
	try
	{
		lumex::read (stream_a, source.bytes);
		lumex::read (stream_a, amount.bytes);
		lumex::read (stream_a, epoch);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

size_t lumex::pending_info::db_size () const
{
	return sizeof (source) + sizeof (amount) + sizeof (epoch);
}

bool lumex::pending_info::operator== (lumex::pending_info const & other_a) const
{
	return source == other_a.source && amount == other_a.amount && epoch == other_a.epoch;
}

lumex::pending_key::pending_key (lumex::account const & account_a, lumex::block_hash const & hash_a) :
	account (account_a),
	hash (hash_a)
{
}

bool lumex::pending_key::deserialize (lumex::stream & stream_a)
{
	auto error (false);
	try
	{
		lumex::read (stream_a, account.bytes);
		lumex::read (stream_a, hash.bytes);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool lumex::pending_key::operator== (lumex::pending_key const & other_a) const
{
	return account == other_a.account && hash == other_a.hash;
}

lumex::account const & lumex::pending_key::key () const
{
	return account;
}

bool lumex::pending_key::operator< (lumex::pending_key const & other_a) const
{
	return account == other_a.account ? hash < other_a.hash : account < other_a.account;
}
