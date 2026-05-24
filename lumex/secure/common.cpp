#include <lumex/crypto_lib/random_pool.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/config.hpp>
#include <lumex/lib/enum_util.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/stream.hpp>
#include <lumex/lib/timer.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/endpoint_key.hpp>
#include <lumex/secure/network_params.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <limits>
#include <queue>

#include <crypto/ed25519-donna/ed25519.h>

/*
 * unchecked_info
 */

lumex::unchecked_info::unchecked_info (std::shared_ptr<lumex::block> const & block_a) :
	block (block_a),
	modified_m (lumex::seconds_since_epoch ())
{
}

void lumex::unchecked_info::serialize (lumex::stream & stream_a) const
{
	debug_assert (block != nullptr);
	lumex::serialize_block (stream_a, *block);
	lumex::write (stream_a, modified_m);
}

bool lumex::unchecked_info::deserialize (lumex::stream & stream_a)
{
	block = lumex::deserialize_block (stream_a);
	bool error (block == nullptr);
	if (!error)
	{
		try
		{
			lumex::read (stream_a, modified_m);
		}
		catch (std::runtime_error const &)
		{
			error = true;
		}
	}
	return error;
}

uint64_t lumex::unchecked_info::modified () const
{
	return modified_m;
}

/*
 * confirmation_height_info
 */

lumex::confirmation_height_info::confirmation_height_info (uint64_t confirmation_height_a, lumex::block_hash const & confirmed_frontier_a) :
	height (confirmation_height_a),
	frontier (confirmed_frontier_a)
{
}

void lumex::confirmation_height_info::serialize (lumex::stream & stream_a) const
{
	lumex::write (stream_a, height);
	lumex::write (stream_a, frontier);
}

bool lumex::confirmation_height_info::deserialize (lumex::stream & stream_a)
{
	auto error (false);
	try
	{
		lumex::read (stream_a, height);
		lumex::read (stream_a, frontier);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

lumex::block_info::block_info (lumex::account const & account_a, lumex::amount const & balance_a) :
	account (account_a),
	balance (balance_a)
{
}

lumex::wallet_id lumex::random_wallet_id ()
{
	lumex::wallet_id wallet_id;
	lumex::uint256_union dummy_secret;
	random_pool::generate_block (dummy_secret.bytes.data (), dummy_secret.bytes.size ());
	ed25519_publickey (dummy_secret.bytes.data (), wallet_id.bytes.data ());
	return wallet_id;
}

/*
 * unchecked_key
 */

lumex::unchecked_key::unchecked_key (lumex::hash_or_account const & dependency) :
	unchecked_key{ dependency, 0 }
{
}

lumex::unchecked_key::unchecked_key (lumex::hash_or_account const & previous_a, lumex::block_hash const & hash_a) :
	previous (previous_a.as_block_hash ()),
	hash (hash_a)
{
}

lumex::unchecked_key::unchecked_key (lumex::uint512_union const & union_a) :
	previous (union_a.uint256s[0].number ()),
	hash (union_a.uint256s[1].number ())
{
}

bool lumex::unchecked_key::deserialize (lumex::stream & stream_a)
{
	auto error (false);
	try
	{
		lumex::read (stream_a, previous.bytes);
		lumex::read (stream_a, hash.bytes);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool lumex::unchecked_key::operator== (lumex::unchecked_key const & other_a) const
{
	return previous == other_a.previous && hash == other_a.hash;
}

bool lumex::unchecked_key::operator< (lumex::unchecked_key const & other_a) const
{
	return previous != other_a.previous ? previous < other_a.previous : hash < other_a.hash;
}

lumex::block_hash const & lumex::unchecked_key::key () const
{
	return previous;
}

/*
 * topo_key
 */

void lumex::topo_key::serialize (lumex::stream & stream_a) const
{
	lumex::write (stream_a, boost::endian::native_to_big (topo_height));
	lumex::write (stream_a, hash.bytes);
}

bool lumex::topo_key::deserialize (lumex::stream & stream_a)
{
	auto error (false);
	try
	{
		lumex::read (stream_a, topo_height);
		boost::endian::big_to_native_inplace (topo_height);
		lumex::read (stream_a, hash.bytes);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

/*
 *
 */

std::string_view lumex::to_string (lumex::block_status code)
{
	return lumex::enum_to_string (code);
}

lumex::stat::detail lumex::to_stat_detail (lumex::block_status code)
{
	return lumex::enum_convert<lumex::stat::detail> (code);
}
