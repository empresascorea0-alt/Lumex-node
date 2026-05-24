#include <lumex/crypto/blake2/blake2.h>
#include <lumex/crypto_lib/random_pool.hpp>
#include <lumex/lib/block_type.hpp>
#include <lumex/lib/block_uniquer.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/enum_util.hpp>
#include <lumex/lib/memory.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/object_stream.hpp>
#include <lumex/lib/stream.hpp>
#include <lumex/lib/threading.hpp>
#include <lumex/lib/work_version.hpp>
#include <lumex/secure/common.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <bitset>

#include <cryptopp/words.h>

size_t constexpr lumex::send_block::size;
size_t constexpr lumex::receive_block::size;
size_t constexpr lumex::open_block::size;
size_t constexpr lumex::change_block::size;
size_t constexpr lumex::state_block::size;

/** Compare blocks, first by type, then content. This is an optimization over dynamic_cast, which is very slow on some platforms. */
namespace
{
template <typename T>
bool blocks_equal (T const & first, lumex::block const & second)
{
	static_assert (std::is_base_of<lumex::block, T>::value, "Input parameter is not a block type");
	return (first.type () == second.type ()) && (static_cast<T const &> (second)) == first;
}

template <typename block>
std::shared_ptr<block> deserialize_block (lumex::stream & stream_a)
{
	auto error (false);
	auto result = lumex::make_shared<block> (error, stream_a);
	if (error)
	{
		result = nullptr;
	}

	return result;
}
}

void lumex::block_memory_pool_purge ()
{
	lumex::purge_shared_ptr_singleton_pool_memory<lumex::open_block> ();
	lumex::purge_shared_ptr_singleton_pool_memory<lumex::state_block> ();
	lumex::purge_shared_ptr_singleton_pool_memory<lumex::send_block> ();
	lumex::purge_shared_ptr_singleton_pool_memory<lumex::change_block> ();
}

/*
 * block
 */

std::string lumex::block::to_json () const
{
	std::string result;
	serialize_json (result);
	return result;
}

size_t lumex::block::size (lumex::block_type type_a)
{
	size_t result (0);
	switch (type_a)
	{
		case lumex::block_type::invalid:
		case lumex::block_type::not_a_block:
			debug_assert (false);
			break;
		case lumex::block_type::send:
			result = lumex::send_block::size;
			break;
		case lumex::block_type::receive:
			result = lumex::receive_block::size;
			break;
		case lumex::block_type::change:
			result = lumex::change_block::size;
			break;
		case lumex::block_type::open:
			result = lumex::open_block::size;
			break;
		case lumex::block_type::state:
			result = lumex::state_block::size;
			break;
	}
	return result;
}

lumex::work_version lumex::block::work_version () const
{
	return lumex::work_version::work_1;
}

lumex::block_hash lumex::block::generate_hash () const
{
	lumex::block_hash result;
	blake2b_state hash_l;
	auto status (blake2b_init (&hash_l, sizeof (result.bytes)));
	debug_assert (status == 0);
	generate_hash (hash_l);
	status = blake2b_final (&hash_l, result.bytes.data (), sizeof (result.bytes));
	debug_assert (status == 0);
	return result;
}

void lumex::block::refresh ()
{
	if (!cached_hash.is_zero ())
	{
		cached_hash = generate_hash ();
	}
}

bool lumex::block::is_send () const noexcept
{
	release_assert (has_sideband ());
	switch (type ())
	{
		case lumex::block_type::send:
			return true;
		case lumex::block_type::state:
			return sideband ().details.is_send;
		default:
			return false;
	}
}

bool lumex::block::is_receive () const noexcept
{
	release_assert (has_sideband ());
	switch (type ())
	{
		case lumex::block_type::receive:
		case lumex::block_type::open:
			return true;
		case lumex::block_type::state:
			return sideband ().details.is_receive;
		default:
			return false;
	}
}

bool lumex::block::is_change () const noexcept
{
	release_assert (has_sideband ());
	switch (type ())
	{
		case lumex::block_type::change:
			return true;
		case lumex::block_type::state:
			if (link_field ().value ().is_zero ())
			{
				return true;
			}
			return false;
		default:
			return false;
	}
}

bool lumex::block::is_epoch () const noexcept
{
	release_assert (has_sideband ());
	switch (type ())
	{
		case lumex::block_type::state:
			return sideband ().details.is_epoch;
		default:
			return false;
	}
}

std::array<lumex::block_hash, 2> lumex::block::dependencies () const
{
	release_assert (has_sideband ());
	std::array<lumex::block_hash, 2> result{ 0, 0 };
	result[0] = previous ();
	if (is_receive ())
	{
		// For genesis block, the source is the genesis account public key (not a real block hash)
		if (type () == lumex::block_type::open && source () == account ().as_union ())
		{
			// Genesis block has no source dependency
		}
		else
		{
			result[1] = source ();
		}
	}
	return result;
}

lumex::block_hash const & lumex::block::hash () const
{
	if (!cached_hash.is_zero ())
	{
		// Once a block is created, it should not be modified (unless using refresh ())
		// This would invalidate the cache; check it hasn't changed.
		debug_assert (cached_hash == generate_hash ());
	}
	else
	{
		cached_hash = generate_hash ();
	}

	return cached_hash;
}

lumex::block_hash lumex::block::full_hash () const
{
	lumex::block_hash result;
	blake2b_state state;
	blake2b_init (&state, sizeof (result.bytes));
	blake2b_update (&state, hash ().bytes.data (), sizeof (hash ()));
	auto signature (block_signature ());
	blake2b_update (&state, signature.bytes.data (), sizeof (signature));
	auto work (block_work ());
	blake2b_update (&state, &work, sizeof (work));
	blake2b_final (&state, result.bytes.data (), sizeof (result.bytes));
	return result;
}

lumex::block_sideband const & lumex::block::sideband () const
{
	release_assert (sideband_m.is_initialized ());
	return *sideband_m;
}

void lumex::block::sideband_set (lumex::block_sideband const & sideband_a)
{
	sideband_m = sideband_a;
}

bool lumex::block::has_sideband () const
{
	return sideband_m.is_initialized ();
}

std::optional<lumex::account> lumex::block::representative_field () const
{
	return std::nullopt;
}

std::optional<lumex::block_hash> lumex::block::source_field () const
{
	return std::nullopt;
}

std::optional<lumex::account> lumex::block::destination_field () const
{
	return std::nullopt;
}

std::optional<lumex::link> lumex::block::link_field () const
{
	return std::nullopt;
}

lumex::account lumex::block::account () const noexcept
{
	release_assert (has_sideband ());
	switch (type ())
	{
		case block_type::open:
		case block_type::state:
			return account_field ().value ();
		case block_type::change:
		case block_type::send:
		case block_type::receive:
			return sideband ().account;
		default:
			release_assert (false);
	}
}

lumex::amount lumex::block::balance () const noexcept
{
	release_assert (has_sideband ());
	switch (type ())
	{
		case lumex::block_type::open:
		case lumex::block_type::receive:
		case lumex::block_type::change:
			return sideband ().balance;
		case lumex::block_type::send:
		case lumex::block_type::state:
			return balance_field ().value ();
		default:
			release_assert (false);
	}
}

lumex::account lumex::block::destination () const noexcept
{
	release_assert (has_sideband ());
	switch (type ())
	{
		case lumex::block_type::send:
			return destination_field ().value ();
		case lumex::block_type::state:
			release_assert (sideband ().details.is_send);
			return link_field ().value ().as_account ();
		default:
			release_assert (false);
	}
}

lumex::block_hash lumex::block::source () const noexcept
{
	release_assert (has_sideband ());
	switch (type ())
	{
		case lumex::block_type::open:
		case lumex::block_type::receive:
			return source_field ().value ();
		case lumex::block_type::state:
			release_assert (sideband ().details.is_receive);
			return link_field ().value ().as_block_hash ();
		default:
			release_assert (false);
	}
}

// TODO - Remove comments below and fixup usages to not need to check .is_zero ()
// std::optional<lumex::block_hash> lumex::block::previous () const
lumex::block_hash lumex::block::previous () const noexcept
{
	std::optional<lumex::block_hash> result = previous_field ();
	/*
	if (result && result.value ().is_zero ())
	{
		return std::nullopt;
	}
	return result;*/
	return result.value_or (0);
}

std::optional<lumex::account> lumex::block::account_field () const
{
	return std::nullopt;
}

lumex::qualified_root lumex::block::qualified_root () const
{
	return { root (), previous () };
}

std::optional<lumex::amount> lumex::block::balance_field () const
{
	return std::nullopt;
}

void lumex::block::operator() (lumex::object_stream & obs) const
{
	obs.write ("type", type ());
	obs.write ("hash", hash ());

	if (has_sideband ())
	{
		obs.write ("sideband", sideband ());
	}
}

/*
 * send_block
 */

void lumex::send_block::visit (lumex::block_visitor & visitor_a) const
{
	visitor_a.send_block (*this);
}

void lumex::send_block::visit (lumex::mutable_block_visitor & visitor_a)
{
	visitor_a.send_block (*this);
}

void lumex::send_block::generate_hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t lumex::send_block::block_work () const
{
	return work;
}

void lumex::send_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

lumex::send_hashables::send_hashables (lumex::block_hash const & previous_a, lumex::account const & destination_a, lumex::amount const & balance_a) :
	previous (previous_a),
	destination (destination_a),
	balance (balance_a)
{
}

lumex::send_hashables::send_hashables (bool & error_a, lumex::stream & stream_a)
{
	try
	{
		lumex::read (stream_a, previous.bytes);
		lumex::read (stream_a, destination.bytes);
		lumex::read (stream_a, balance.bytes);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

lumex::send_hashables::send_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto destination_l (tree_a.get<std::string> ("destination"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		error_a = previous.decode_hex (previous_l);
		if (!error_a)
		{
			error_a = destination.decode_account (destination_l);
			if (!error_a)
			{
				error_a = balance.decode_hex (balance_l);
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void lumex::send_hashables::hash (blake2b_state & hash_a) const
{
	auto status (blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes)));
	debug_assert (status == 0);
	status = blake2b_update (&hash_a, destination.bytes.data (), sizeof (destination.bytes));
	debug_assert (status == 0);
	status = blake2b_update (&hash_a, balance.bytes.data (), sizeof (balance.bytes));
	debug_assert (status == 0);
}

void lumex::send_block::serialize (lumex::stream & stream_a) const
{
	write (stream_a, hashables.previous.bytes);
	write (stream_a, hashables.destination.bytes);
	write (stream_a, hashables.balance.bytes);
	write (stream_a, signature.bytes);
	write (stream_a, work);
}

bool lumex::send_block::deserialize (lumex::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.previous.bytes);
		read (stream_a, hashables.destination.bytes);
		read (stream_a, hashables.balance.bytes);
		read (stream_a, signature.bytes);
		read (stream_a, work);
	}
	catch (std::exception const &)
	{
		error = true;
	}

	return error;
}

void lumex::send_block::serialize_json (std::string & string_a, bool single_line) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree, !single_line);
	string_a = ostream.str ();
}

void lumex::send_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "send");
	tree.put ("previous", hashables.previous.to_string ());
	tree.put ("destination", hashables.destination.to_account ());
	tree.put ("balance", hashables.balance.to_string ());
	tree.put ("work", lumex::to_string_hex (work));
	tree.put ("signature", signature.to_string ());
}

bool lumex::send_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		debug_assert (tree_a.get<std::string> ("type") == "send");
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto destination_l (tree_a.get<std::string> ("destination"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.previous.decode_hex (previous_l);
		if (!error)
		{
			error = hashables.destination.decode_account (destination_l);
			if (!error)
			{
				error = hashables.balance.decode_hex (balance_l);
				if (!error)
				{
					error = lumex::from_string_hex (work_l, work);
					if (!error)
					{
						error = signature.decode_hex (signature_l);
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

lumex::send_block::send_block (lumex::block_hash const & previous_a, lumex::account const & destination_a, lumex::amount const & balance_a, lumex::raw_key const & prv_a, lumex::public_key const & pub_a, uint64_t work_a) :
	hashables (previous_a, destination_a, balance_a),
	signature (lumex::sign_message (prv_a, pub_a, hash ())),
	work (work_a)
{
	debug_assert (destination_a != nullptr);
	debug_assert (pub_a != nullptr);
}

lumex::send_block::send_block (bool & error_a, lumex::stream & stream_a) :
	hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			lumex::read (stream_a, signature.bytes);
			lumex::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

lumex::send_block::send_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
	hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto signature_l (tree_a.get<std::string> ("signature"));
			auto work_l (tree_a.get<std::string> ("work"));
			error_a = signature.decode_hex (signature_l);
			if (!error_a)
			{
				error_a = lumex::from_string_hex (work_l, work);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

std::shared_ptr<lumex::block> lumex::send_block::clone () const
{
	return std::make_shared<lumex::send_block> (*this);
}

bool lumex::send_block::operator== (lumex::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool lumex::send_block::valid_predecessor (lumex::block const & block_a) const
{
	bool result;
	switch (block_a.type ())
	{
		case lumex::block_type::send:
		case lumex::block_type::receive:
		case lumex::block_type::open:
		case lumex::block_type::change:
			result = true;
			break;
		default:
			result = false;
			break;
	}
	return result;
}

lumex::block_type lumex::send_block::type () const
{
	return lumex::block_type::send;
}

bool lumex::send_block::operator== (lumex::send_block const & other_a) const
{
	auto result (hashables.destination == other_a.hashables.destination && hashables.previous == other_a.hashables.previous && hashables.balance == other_a.hashables.balance && work == other_a.work && signature == other_a.signature);
	return result;
}

std::optional<lumex::block_hash> lumex::send_block::previous_field () const
{
	return hashables.previous;
}

std::optional<lumex::account> lumex::send_block::destination_field () const
{
	return hashables.destination;
}

lumex::root lumex::send_block::root () const
{
	return hashables.previous;
}

std::optional<lumex::amount> lumex::send_block::balance_field () const
{
	return hashables.balance;
}

lumex::signature const & lumex::send_block::block_signature () const
{
	return signature;
}

void lumex::send_block::signature_set (lumex::signature const & signature_a)
{
	signature = signature_a;
}

void lumex::send_block::operator() (lumex::object_stream & obs) const
{
	lumex::block::operator() (obs); // Write common data

	obs.write ("previous", hashables.previous);
	obs.write ("destination", hashables.destination);
	obs.write ("balance", hashables.balance);
	obs.write ("signature", signature);
	obs.write ("work", work);
}

/*
 * open_block
 */

lumex::open_hashables::open_hashables (lumex::block_hash const & source_a, lumex::account const & representative_a, lumex::account const & account_a) :
	source (source_a),
	representative (representative_a),
	account (account_a)
{
}

lumex::open_hashables::open_hashables (bool & error_a, lumex::stream & stream_a)
{
	try
	{
		lumex::read (stream_a, source.bytes);
		lumex::read (stream_a, representative.bytes);
		lumex::read (stream_a, account.bytes);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

lumex::open_hashables::open_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto source_l (tree_a.get<std::string> ("source"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto account_l (tree_a.get<std::string> ("account"));
		error_a = source.decode_hex (source_l);
		if (!error_a)
		{
			error_a = representative.decode_account (representative_l);
			if (!error_a)
			{
				error_a = account.decode_account (account_l);
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void lumex::open_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, source.bytes.data (), sizeof (source.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
	blake2b_update (&hash_a, account.bytes.data (), sizeof (account.bytes));
}

lumex::open_block::open_block (lumex::block_hash const & source_a, lumex::account const & representative_a, lumex::account const & account_a, lumex::raw_key const & prv_a, lumex::public_key const & pub_a, uint64_t work_a) :
	hashables (source_a, representative_a, account_a),
	signature (lumex::sign_message (prv_a, pub_a, hash ())),
	work (work_a)
{
	debug_assert (representative_a != nullptr);
	debug_assert (account_a != nullptr);
	debug_assert (pub_a != nullptr);
}

lumex::open_block::open_block (lumex::block_hash const & source_a, lumex::account const & representative_a, lumex::account const & account_a, std::nullptr_t) :
	hashables (source_a, representative_a, account_a),
	work (0)
{
	debug_assert (representative_a != nullptr);
	debug_assert (account_a != nullptr);

	signature.clear ();
}

lumex::open_block::open_block (bool & error_a, lumex::stream & stream_a) :
	hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			lumex::read (stream_a, signature);
			lumex::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

lumex::open_block::open_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
	hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto work_l (tree_a.get<std::string> ("work"));
			auto signature_l (tree_a.get<std::string> ("signature"));
			error_a = lumex::from_string_hex (work_l, work);
			if (!error_a)
			{
				error_a = signature.decode_hex (signature_l);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

std::shared_ptr<lumex::block> lumex::open_block::clone () const
{
	return std::make_shared<lumex::open_block> (*this);
}

void lumex::open_block::generate_hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t lumex::open_block::block_work () const
{
	return work;
}

void lumex::open_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

std::optional<lumex::block_hash> lumex::open_block::previous_field () const
{
	return std::nullopt;
}

std::optional<lumex::account> lumex::open_block::account_field () const
{
	return hashables.account;
}

void lumex::open_block::serialize (lumex::stream & stream_a) const
{
	write (stream_a, hashables.source);
	write (stream_a, hashables.representative);
	write (stream_a, hashables.account);
	write (stream_a, signature);
	write (stream_a, work);
}

bool lumex::open_block::deserialize (lumex::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.source);
		read (stream_a, hashables.representative);
		read (stream_a, hashables.account);
		read (stream_a, signature);
		read (stream_a, work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void lumex::open_block::serialize_json (std::string & string_a, bool single_line) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree, !single_line);
	string_a = ostream.str ();
}

void lumex::open_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "open");
	tree.put ("source", hashables.source.to_string ());
	tree.put ("representative", hashables.representative.to_account ());
	tree.put ("account", hashables.account.to_account ());
	tree.put ("work", lumex::to_string_hex (work));
	tree.put ("signature", signature.to_string ());
}

bool lumex::open_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		debug_assert (tree_a.get<std::string> ("type") == "open");
		auto source_l (tree_a.get<std::string> ("source"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto account_l (tree_a.get<std::string> ("account"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.source.decode_hex (source_l);
		if (!error)
		{
			error = hashables.representative.decode_hex (representative_l);
			if (!error)
			{
				error = hashables.account.decode_hex (account_l);
				if (!error)
				{
					error = lumex::from_string_hex (work_l, work);
					if (!error)
					{
						error = signature.decode_hex (signature_l);
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void lumex::open_block::visit (lumex::block_visitor & visitor_a) const
{
	visitor_a.open_block (*this);
}

void lumex::open_block::visit (lumex::mutable_block_visitor & visitor_a)
{
	visitor_a.open_block (*this);
}

lumex::block_type lumex::open_block::type () const
{
	return lumex::block_type::open;
}

bool lumex::open_block::operator== (lumex::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool lumex::open_block::operator== (lumex::open_block const & other_a) const
{
	return hashables.source == other_a.hashables.source && hashables.representative == other_a.hashables.representative && hashables.account == other_a.hashables.account && work == other_a.work && signature == other_a.signature;
}

bool lumex::open_block::valid_predecessor (lumex::block const & block_a) const
{
	return false;
}

std::optional<lumex::block_hash> lumex::open_block::source_field () const
{
	return hashables.source;
}

lumex::root lumex::open_block::root () const
{
	return hashables.account;
}

std::optional<lumex::account> lumex::open_block::representative_field () const
{
	return hashables.representative;
}

lumex::signature const & lumex::open_block::block_signature () const
{
	return signature;
}

void lumex::open_block::signature_set (lumex::signature const & signature_a)
{
	signature = signature_a;
}

void lumex::open_block::operator() (lumex::object_stream & obs) const
{
	lumex::block::operator() (obs); // Write common data

	obs.write ("source", hashables.source);
	obs.write ("representative", hashables.representative);
	obs.write ("account", hashables.account);
	obs.write ("signature", signature);
	obs.write ("work", work);
}

/*
 * change_block
 */

lumex::change_hashables::change_hashables (lumex::block_hash const & previous_a, lumex::account const & representative_a) :
	previous (previous_a),
	representative (representative_a)
{
}

lumex::change_hashables::change_hashables (bool & error_a, lumex::stream & stream_a)
{
	try
	{
		lumex::read (stream_a, previous);
		lumex::read (stream_a, representative);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

lumex::change_hashables::change_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		error_a = previous.decode_hex (previous_l);
		if (!error_a)
		{
			error_a = representative.decode_account (representative_l);
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void lumex::change_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
}

lumex::change_block::change_block (lumex::block_hash const & previous_a, lumex::account const & representative_a, lumex::raw_key const & prv_a, lumex::public_key const & pub_a, uint64_t work_a) :
	hashables (previous_a, representative_a),
	signature (lumex::sign_message (prv_a, pub_a, hash ())),
	work (work_a)
{
	debug_assert (representative_a != nullptr);
	debug_assert (pub_a != nullptr);
}

lumex::change_block::change_block (bool & error_a, lumex::stream & stream_a) :
	hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			lumex::read (stream_a, signature);
			lumex::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

lumex::change_block::change_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
	hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto work_l (tree_a.get<std::string> ("work"));
			auto signature_l (tree_a.get<std::string> ("signature"));
			error_a = lumex::from_string_hex (work_l, work);
			if (!error_a)
			{
				error_a = signature.decode_hex (signature_l);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

std::shared_ptr<lumex::block> lumex::change_block::clone () const
{
	return std::make_shared<lumex::change_block> (*this);
}

void lumex::change_block::generate_hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t lumex::change_block::block_work () const
{
	return work;
}

void lumex::change_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

std::optional<lumex::block_hash> lumex::change_block::previous_field () const
{
	return hashables.previous;
}

void lumex::change_block::serialize (lumex::stream & stream_a) const
{
	write (stream_a, hashables.previous);
	write (stream_a, hashables.representative);
	write (stream_a, signature);
	write (stream_a, work);
}

bool lumex::change_block::deserialize (lumex::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.previous);
		read (stream_a, hashables.representative);
		read (stream_a, signature);
		read (stream_a, work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void lumex::change_block::serialize_json (std::string & string_a, bool single_line) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree, !single_line);
	string_a = ostream.str ();
}

void lumex::change_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "change");
	tree.put ("previous", hashables.previous.to_string ());
	tree.put ("representative", hashables.representative.to_account ());
	tree.put ("work", lumex::to_string_hex (work));
	tree.put ("signature", signature.to_string ());
}

bool lumex::change_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		debug_assert (tree_a.get<std::string> ("type") == "change");
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.previous.decode_hex (previous_l);
		if (!error)
		{
			error = hashables.representative.decode_hex (representative_l);
			if (!error)
			{
				error = lumex::from_string_hex (work_l, work);
				if (!error)
				{
					error = signature.decode_hex (signature_l);
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void lumex::change_block::visit (lumex::block_visitor & visitor_a) const
{
	visitor_a.change_block (*this);
}

void lumex::change_block::visit (lumex::mutable_block_visitor & visitor_a)
{
	visitor_a.change_block (*this);
}

lumex::block_type lumex::change_block::type () const
{
	return lumex::block_type::change;
}

bool lumex::change_block::operator== (lumex::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool lumex::change_block::operator== (lumex::change_block const & other_a) const
{
	return hashables.previous == other_a.hashables.previous && hashables.representative == other_a.hashables.representative && work == other_a.work && signature == other_a.signature;
}

bool lumex::change_block::valid_predecessor (lumex::block const & block_a) const
{
	bool result;
	switch (block_a.type ())
	{
		case lumex::block_type::send:
		case lumex::block_type::receive:
		case lumex::block_type::open:
		case lumex::block_type::change:
			result = true;
			break;
		default:
			result = false;
			break;
	}
	return result;
}

lumex::root lumex::change_block::root () const
{
	return hashables.previous;
}

std::optional<lumex::account> lumex::change_block::representative_field () const
{
	return hashables.representative;
}

lumex::signature const & lumex::change_block::block_signature () const
{
	return signature;
}

void lumex::change_block::signature_set (lumex::signature const & signature_a)
{
	signature = signature_a;
}

void lumex::change_block::operator() (lumex::object_stream & obs) const
{
	lumex::block::operator() (obs); // Write common data

	obs.write ("previous", hashables.previous);
	obs.write ("representative", hashables.representative);
	obs.write ("signature", signature);
	obs.write ("work", work);
}

/*
 * state_block
 */

lumex::state_hashables::state_hashables (lumex::account const & account_a, lumex::block_hash const & previous_a, lumex::account const & representative_a, lumex::amount const & balance_a, lumex::link const & link_a) :
	account (account_a),
	previous (previous_a),
	representative (representative_a),
	balance (balance_a),
	link (link_a)
{
}

lumex::state_hashables::state_hashables (bool & error_a, lumex::stream & stream_a)
{
	try
	{
		lumex::read (stream_a, account);
		lumex::read (stream_a, previous);
		lumex::read (stream_a, representative);
		lumex::read (stream_a, balance);
		lumex::read (stream_a, link);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

lumex::state_hashables::state_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto account_l (tree_a.get<std::string> ("account"));
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		auto link_l (tree_a.get<std::string> ("link"));
		error_a = account.decode_account (account_l);
		if (!error_a)
		{
			error_a = previous.decode_hex (previous_l);
			if (!error_a)
			{
				error_a = representative.decode_account (representative_l);
				if (!error_a)
				{
					error_a = balance.decode_dec (balance_l);
					if (!error_a)
					{
						error_a = link.decode_account (link_l) && link.decode_hex (link_l);
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void lumex::state_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, account.bytes.data (), sizeof (account.bytes));
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
	blake2b_update (&hash_a, balance.bytes.data (), sizeof (balance.bytes));
	blake2b_update (&hash_a, link.bytes.data (), sizeof (link.bytes));
}

lumex::state_block::state_block (lumex::account const & account_a, lumex::block_hash const & previous_a, lumex::account const & representative_a, lumex::amount const & balance_a, lumex::link const & link_a, lumex::raw_key const & prv_a, lumex::public_key const & pub_a, uint64_t work_a) :
	hashables (account_a, previous_a, representative_a, balance_a, link_a),
	signature (lumex::sign_message (prv_a, pub_a, hash ())),
	work (work_a)
{
	debug_assert (account_a != nullptr);
	debug_assert (representative_a != nullptr);
	debug_assert (link_a.as_account () != nullptr);
	debug_assert (pub_a != nullptr);
}

lumex::state_block::state_block (bool & error_a, lumex::stream & stream_a) :
	hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			lumex::read (stream_a, signature);
			lumex::read (stream_a, work);
			boost::endian::big_to_native_inplace (work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

lumex::state_block::state_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
	hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto type_l (tree_a.get<std::string> ("type"));
			auto signature_l (tree_a.get<std::string> ("signature"));
			auto work_l (tree_a.get<std::string> ("work"));
			error_a = type_l != "state";
			if (!error_a)
			{
				error_a = lumex::from_string_hex (work_l, work);
				if (!error_a)
				{
					error_a = signature.decode_hex (signature_l);
				}
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

std::shared_ptr<lumex::block> lumex::state_block::clone () const
{
	return std::make_shared<lumex::state_block> (*this);
}

void lumex::state_block::generate_hash (blake2b_state & hash_a) const
{
	lumex::uint256_union preamble (static_cast<uint64_t> (lumex::block_type::state));
	blake2b_update (&hash_a, preamble.bytes.data (), preamble.bytes.size ());
	hashables.hash (hash_a);
}

uint64_t lumex::state_block::block_work () const
{
	return work;
}

void lumex::state_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

std::optional<lumex::block_hash> lumex::state_block::previous_field () const
{
	return hashables.previous;
}

std::optional<lumex::account> lumex::state_block::account_field () const
{
	return hashables.account;
}

void lumex::state_block::serialize (lumex::stream & stream_a) const
{
	write (stream_a, hashables.account);
	write (stream_a, hashables.previous);
	write (stream_a, hashables.representative);
	write (stream_a, hashables.balance);
	write (stream_a, hashables.link);
	write (stream_a, signature);
	write (stream_a, boost::endian::native_to_big (work));
}

bool lumex::state_block::deserialize (lumex::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.account);
		read (stream_a, hashables.previous);
		read (stream_a, hashables.representative);
		read (stream_a, hashables.balance);
		read (stream_a, hashables.link);
		read (stream_a, signature);
		read (stream_a, work);
		boost::endian::big_to_native_inplace (work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void lumex::state_block::serialize_json (std::string & string_a, bool single_line) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree, !single_line);
	string_a = ostream.str ();
}

void lumex::state_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "state");
	tree.put ("account", hashables.account.to_account ());
	tree.put ("previous", hashables.previous.to_string ());
	tree.put ("representative", hashables.representative.to_account ());
	tree.put ("balance", hashables.balance.to_string_dec ());
	tree.put ("link", hashables.link.to_string ());
	tree.put ("link_as_account", hashables.link.to_account ());
	tree.put ("signature", signature.to_string ());
	tree.put ("work", lumex::to_string_hex (work));
}

bool lumex::state_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		debug_assert (tree_a.get<std::string> ("type") == "state");
		auto account_l (tree_a.get<std::string> ("account"));
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		auto link_l (tree_a.get<std::string> ("link"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.account.decode_account (account_l);
		if (!error)
		{
			error = hashables.previous.decode_hex (previous_l);
			if (!error)
			{
				error = hashables.representative.decode_account (representative_l);
				if (!error)
				{
					error = hashables.balance.decode_dec (balance_l);
					if (!error)
					{
						error = hashables.link.decode_account (link_l) && hashables.link.decode_hex (link_l);
						if (!error)
						{
							error = lumex::from_string_hex (work_l, work);
							if (!error)
							{
								error = signature.decode_hex (signature_l);
							}
						}
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void lumex::state_block::visit (lumex::block_visitor & visitor_a) const
{
	visitor_a.state_block (*this);
}

void lumex::state_block::visit (lumex::mutable_block_visitor & visitor_a)
{
	visitor_a.state_block (*this);
}

lumex::block_type lumex::state_block::type () const
{
	return lumex::block_type::state;
}

bool lumex::state_block::operator== (lumex::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool lumex::state_block::operator== (lumex::state_block const & other_a) const
{
	return hashables.account == other_a.hashables.account && hashables.previous == other_a.hashables.previous && hashables.representative == other_a.hashables.representative && hashables.balance == other_a.hashables.balance && hashables.link == other_a.hashables.link && signature == other_a.signature && work == other_a.work;
}

bool lumex::state_block::valid_predecessor (lumex::block const & block_a) const
{
	return true;
}

lumex::root lumex::state_block::root () const
{
	if (!hashables.previous.is_zero ())
	{
		return hashables.previous;
	}
	else
	{
		return hashables.account;
	}
}

std::optional<lumex::link> lumex::state_block::link_field () const
{
	return hashables.link;
}

std::optional<lumex::account> lumex::state_block::representative_field () const
{
	return hashables.representative;
}

std::optional<lumex::amount> lumex::state_block::balance_field () const
{
	return hashables.balance;
}

lumex::signature const & lumex::state_block::block_signature () const
{
	return signature;
}

void lumex::state_block::signature_set (lumex::signature const & signature_a)
{
	signature = signature_a;
}

void lumex::state_block::operator() (lumex::object_stream & obs) const
{
	lumex::block::operator() (obs); // Write common data

	obs.write ("account", hashables.account);
	obs.write ("previous", hashables.previous);
	obs.write ("representative", hashables.representative);
	obs.write ("balance", hashables.balance);
	obs.write ("link", hashables.link);
	obs.write ("signature", signature);
	obs.write ("work", work);
}

/*
 *
 */

std::shared_ptr<lumex::block> lumex::deserialize_block_json (boost::property_tree::ptree const & tree_a, lumex::block_uniquer * uniquer_a)
{
	std::shared_ptr<lumex::block> result;
	try
	{
		auto type (tree_a.get<std::string> ("type"));
		bool error (false);
		std::unique_ptr<lumex::block> obj;
		if (type == "receive")
		{
			obj = std::make_unique<lumex::receive_block> (error, tree_a);
		}
		else if (type == "send")
		{
			obj = std::make_unique<lumex::send_block> (error, tree_a);
		}
		else if (type == "open")
		{
			obj = std::make_unique<lumex::open_block> (error, tree_a);
		}
		else if (type == "change")
		{
			obj = std::make_unique<lumex::change_block> (error, tree_a);
		}
		else if (type == "state")
		{
			obj = std::make_unique<lumex::state_block> (error, tree_a);
		}

		if (!error)
		{
			result = std::move (obj);
		}
	}
	catch (std::runtime_error const &)
	{
	}
	if (uniquer_a != nullptr)
	{
		result = uniquer_a->unique (result);
	}
	return result;
}

void lumex::serialize_block (lumex::stream & stream_a, lumex::block const & block_a)
{
	lumex::write (stream_a, block_a.type ());
	block_a.serialize (stream_a);
}

std::shared_ptr<lumex::block> lumex::deserialize_block (lumex::stream & stream_a)
{
	lumex::block_type type;
	auto error (try_read (stream_a, type));
	std::shared_ptr<lumex::block> result;
	if (!error)
	{
		result = lumex::deserialize_block (stream_a, type);
	}
	return result;
}

std::shared_ptr<lumex::block> lumex::deserialize_block (lumex::stream & stream_a, lumex::block_type type_a, lumex::block_uniquer * uniquer_a)
{
	std::shared_ptr<lumex::block> result;
	switch (type_a)
	{
		case lumex::block_type::receive:
		{
			result = ::deserialize_block<lumex::receive_block> (stream_a);
			break;
		}
		case lumex::block_type::send:
		{
			result = ::deserialize_block<lumex::send_block> (stream_a);
			break;
		}
		case lumex::block_type::open:
		{
			result = ::deserialize_block<lumex::open_block> (stream_a);
			break;
		}
		case lumex::block_type::change:
		{
			result = ::deserialize_block<lumex::change_block> (stream_a);
			break;
		}
		case lumex::block_type::state:
		{
			result = ::deserialize_block<lumex::state_block> (stream_a);
			break;
		}
		default:
		{
			return {};
		}
	}
	if (result && uniquer_a != nullptr)
	{
		result = uniquer_a->unique (result);
	}
	return result;
}

/*
 * receive_block
 */

void lumex::receive_block::visit (lumex::block_visitor & visitor_a) const
{
	visitor_a.receive_block (*this);
}

void lumex::receive_block::visit (lumex::mutable_block_visitor & visitor_a)
{
	visitor_a.receive_block (*this);
}

bool lumex::receive_block::operator== (lumex::receive_block const & other_a) const
{
	auto result (hashables.previous == other_a.hashables.previous && hashables.source == other_a.hashables.source && work == other_a.work && signature == other_a.signature);
	return result;
}

void lumex::receive_block::serialize (lumex::stream & stream_a) const
{
	write (stream_a, hashables.previous.bytes);
	write (stream_a, hashables.source.bytes);
	write (stream_a, signature.bytes);
	write (stream_a, work);
}

bool lumex::receive_block::deserialize (lumex::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.previous.bytes);
		read (stream_a, hashables.source.bytes);
		read (stream_a, signature.bytes);
		read (stream_a, work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void lumex::receive_block::serialize_json (std::string & string_a, bool single_line) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree, !single_line);
	string_a = ostream.str ();
}

void lumex::receive_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "receive");
	tree.put ("previous", hashables.previous.to_string ());
	tree.put ("source", hashables.source.to_string ());
	tree.put ("work", lumex::to_string_hex (work));
	tree.put ("signature", signature.to_string ());
}

bool lumex::receive_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		debug_assert (tree_a.get<std::string> ("type") == "receive");
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto source_l (tree_a.get<std::string> ("source"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.previous.decode_hex (previous_l);
		if (!error)
		{
			error = hashables.source.decode_hex (source_l);
			if (!error)
			{
				error = lumex::from_string_hex (work_l, work);
				if (!error)
				{
					error = signature.decode_hex (signature_l);
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

lumex::receive_block::receive_block (lumex::block_hash const & previous_a, lumex::block_hash const & source_a, lumex::raw_key const & prv_a, lumex::public_key const & pub_a, uint64_t work_a) :
	hashables (previous_a, source_a),
	signature (lumex::sign_message (prv_a, pub_a, hash ())),
	work (work_a)
{
	debug_assert (pub_a != nullptr);
}

lumex::receive_block::receive_block (bool & error_a, lumex::stream & stream_a) :
	hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			lumex::read (stream_a, signature);
			lumex::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

lumex::receive_block::receive_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
	hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto signature_l (tree_a.get<std::string> ("signature"));
			auto work_l (tree_a.get<std::string> ("work"));
			error_a = signature.decode_hex (signature_l);
			if (!error_a)
			{
				error_a = lumex::from_string_hex (work_l, work);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

std::shared_ptr<lumex::block> lumex::receive_block::clone () const
{
	return std::make_shared<lumex::receive_block> (*this);
}

void lumex::receive_block::generate_hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t lumex::receive_block::block_work () const
{
	return work;
}

void lumex::receive_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

bool lumex::receive_block::operator== (lumex::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool lumex::receive_block::valid_predecessor (lumex::block const & block_a) const
{
	bool result;
	switch (block_a.type ())
	{
		case lumex::block_type::send:
		case lumex::block_type::receive:
		case lumex::block_type::open:
		case lumex::block_type::change:
			result = true;
			break;
		default:
			result = false;
			break;
	}
	return result;
}

std::optional<lumex::block_hash> lumex::receive_block::previous_field () const
{
	return hashables.previous;
}

std::optional<lumex::block_hash> lumex::receive_block::source_field () const
{
	return hashables.source;
}

lumex::root lumex::receive_block::root () const
{
	return hashables.previous;
}

lumex::signature const & lumex::receive_block::block_signature () const
{
	return signature;
}

void lumex::receive_block::signature_set (lumex::signature const & signature_a)
{
	signature = signature_a;
}

lumex::block_type lumex::receive_block::type () const
{
	return lumex::block_type::receive;
}

lumex::receive_hashables::receive_hashables (lumex::block_hash const & previous_a, lumex::block_hash const & source_a) :
	previous (previous_a),
	source (source_a)
{
}

lumex::receive_hashables::receive_hashables (bool & error_a, lumex::stream & stream_a)
{
	try
	{
		lumex::read (stream_a, previous.bytes);
		lumex::read (stream_a, source.bytes);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

lumex::receive_hashables::receive_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto source_l (tree_a.get<std::string> ("source"));
		error_a = previous.decode_hex (previous_l);
		if (!error_a)
		{
			error_a = source.decode_hex (source_l);
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void lumex::receive_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, source.bytes.data (), sizeof (source.bytes));
}

void lumex::receive_block::operator() (lumex::object_stream & obs) const
{
	lumex::block::operator() (obs); // Write common data

	obs.write ("previous", hashables.previous);
	obs.write ("source", hashables.source);
	obs.write ("signature", signature);
	obs.write ("work", work);
}
