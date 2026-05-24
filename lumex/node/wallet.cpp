#include <lumex/crypto_lib/random_pool.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/files.hpp>
#include <lumex/lib/formatting.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/threading.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/lib/work_version.hpp>
#include <lumex/node/cementing_set.hpp>
#include <lumex/node/election.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/transport/traffic_type.hpp>
#include <lumex/node/wallet.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/ledger_set_any.hpp>
#include <lumex/secure/ledger_set_cemented.hpp>
#include <lumex/store/ledger/pending.hpp>
#include <lumex/store/typed_iterator_templ.hpp>
#include <lumex/wallet/wallet_value.hpp>
#include <lumex/wallet/wallets_backend.hpp>

#include <boost/format.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <future>
#include <stdexcept>
#include <type_traits>

template class lumex::store::typed_iterator<lumex::account, lumex::wallet::wallet_value>;

namespace lumex::wallet
{
/*
 * wallet_cipher
 */

wallet_cipher::wallet_cipher (lumex::raw_key wallet_key_a) :
	wallet_key{ wallet_key_a }
{
}

lumex::raw_key wallet_cipher::encrypt (lumex::raw_key const & plaintext, lumex::uint128_union const & iv) const
{
	lumex::raw_key result;
	result.encrypt (plaintext, wallet_key, iv);
	return result;
}

lumex::raw_key wallet_cipher::decrypt (lumex::uint256_union const & ciphertext, lumex::uint128_union const & iv) const
{
	lumex::raw_key result;
	result.decrypt (ciphertext, wallet_key, iv);
	return result;
}

lumex::raw_key wallet_cipher::reseal (lumex::raw_key const & new_password_key, lumex::uint128_union const & iv) const
{
	lumex::raw_key result;
	result.encrypt (wallet_key, new_password_key, iv);
	return result;
}

/*
 * wallet_store
 */

// Wallet version number
lumex::account const wallet_store::version_special{};
// Random number used to salt private key encryption
lumex::account const wallet_store::salt_special (1);
// Key used to encrypt wallet keys, encrypted itself by the user password
lumex::account const wallet_store::wallet_key_special (2);
// Check value used to see if password is valid
lumex::account const wallet_store::check_special (3);
// Representative account to be used if we open a new account
lumex::account const wallet_store::representative_special (4);
// Wallet seed for deterministic key generation
lumex::account const wallet_store::seed_special (5);
// Current key index for deterministic keys
lumex::account const wallet_store::deterministic_index_special (6);
int const wallet_store::special_count (7);
std::string const wallet_store::default_password{ "" };
std::size_t const wallet_store::check_iv_index (0);
std::size_t const wallet_store::seed_iv_index (1);

wallet_store::wallet_store (lumex::kdf & kdf_a, lumex::store::write_transaction & transaction_a, lumex::wallet::wallets_backend & backend_a, unsigned fanout_a, std::string const & wallet_a, std::string const & json_a) :
	password (0, fanout_a),
	wallet_key_mem (0, fanout_a),
	kdf (kdf_a),
	backend{ backend_a }
{
	handle = backend.wallet_open_or_create (transaction_a, wallet_a);
	try
	{
		release_assert (!backend.entry_exists (transaction_a, handle, version_special), "wallet already exists before import");
		boost::property_tree::ptree wallet_l;
		std::stringstream istream (json_a);
		try
		{
			boost::property_tree::read_json (istream, wallet_l);
		}
		catch (...)
		{
			throw std::runtime_error ("Failed to parse wallet JSON");
		}
		for (auto i (wallet_l.begin ()), n (wallet_l.end ()); i != n; ++i)
		{
			lumex::account key;
			if (key.decode_hex (i->first))
			{
				throw std::runtime_error ("Failed to decode wallet key hex");
			}
			lumex::raw_key value;
			if (value.decode_hex (wallet_l.get<std::string> (i->first)))
			{
				throw std::runtime_error ("Failed to decode wallet value hex");
			}
			entry_put_raw (transaction_a, key, lumex::wallet::wallet_value (value, 0));
		}
		bool missing = false;
		missing |= !backend.entry_exists (transaction_a, handle, version_special);
		missing |= !backend.entry_exists (transaction_a, handle, wallet_key_special);
		missing |= !backend.entry_exists (transaction_a, handle, salt_special);
		missing |= !backend.entry_exists (transaction_a, handle, check_special);
		missing |= !backend.entry_exists (transaction_a, handle, representative_special);
		if (missing)
		{
			throw std::runtime_error ("Wallet is missing required entries");
		}
		lumex::raw_key key;
		key.clear ();
		password.value_set (key);
		key = entry_get_raw (transaction_a, wallet_store::wallet_key_special).key;
		wallet_key_mem.value_set (key);
		attempt_password (transaction_a, default_password);
	}
	catch (...)
	{
		destroy (transaction_a);
		throw;
	}
}

wallet_store::wallet_store (lumex::kdf & kdf_a, lumex::store::write_transaction & transaction_a, lumex::wallet::wallets_backend & backend_a, lumex::account representative_a, unsigned fanout_a, std::string const & wallet_a) :
	password (0, fanout_a),
	wallet_key_mem (0, fanout_a),
	kdf (kdf_a),
	backend{ backend_a }
{
	handle = backend.wallet_open_or_create (transaction_a, wallet_a);
	try
	{
		if (!backend.entry_exists (transaction_a, handle, version_special))
		{
			version_put (transaction_a, version_current);
			lumex::raw_key salt_l;
			random_pool::generate_block (salt_l.bytes.data (), salt_l.bytes.size ());
			entry_put_raw (transaction_a, wallet_store::salt_special, lumex::wallet::wallet_value (salt_l, 0));
			// Wallet key is a fixed random key that encrypts all entries
			lumex::raw_key wallet_key;
			random_pool::generate_block (wallet_key.bytes.data (), sizeof (wallet_key.bytes));
			auto password_l = derive_key (transaction_a, default_password);
			password.value_set (password_l);
			// Wallet key is encrypted by the user's password
			lumex::raw_key encrypted;
			encrypted.encrypt (wallet_key, password_l, salt_l.owords[0]);
			entry_put_raw (transaction_a, wallet_store::wallet_key_special, lumex::wallet::wallet_value (encrypted, 0));
			lumex::raw_key wallet_key_enc;
			wallet_key_enc = encrypted;
			wallet_key_mem.value_set (wallet_key_enc);
			lumex::raw_key zero;
			zero.clear ();
			lumex::raw_key check;
			check.encrypt (zero, wallet_key, salt_l.owords[check_iv_index]);
			entry_put_raw (transaction_a, wallet_store::check_special, lumex::wallet::wallet_value (check, 0));
			lumex::raw_key rep;
			rep.bytes = representative_a.bytes;
			entry_put_raw (transaction_a, wallet_store::representative_special, lumex::wallet::wallet_value (rep, 0));
			lumex::raw_key seed;
			random_pool::generate_block (seed.bytes.data (), seed.bytes.size ());
			seed_set (transaction_a, seed);
			entry_put_raw (transaction_a, wallet_store::deterministic_index_special, lumex::wallet::wallet_value (0, 0));
		}
		lumex::raw_key key;
		key = entry_get_raw (transaction_a, wallet_store::wallet_key_special).key;
		wallet_key_mem.value_set (key);
		attempt_password (transaction_a, default_password);
	}
	catch (...)
	{
		destroy (transaction_a);
		throw;
	}
}

std::vector<lumex::account> wallet_store::accounts (lumex::store::transaction const & transaction_a) const
{
	std::vector<lumex::account> result;
	for (auto i (begin (transaction_a)), n (end (transaction_a)); i != n; ++i)
	{
		lumex::account const & account (i->first);
		result.push_back (account);
	}
	return result;
}

void wallet_store::destroy (lumex::store::write_transaction const & transaction_a)
{
	backend.wallet_drop (transaction_a, handle.lock ().get ());
}

bool wallet_store::is_representative (lumex::store::transaction const & transaction_a) const
{
	return exists (transaction_a, representative (transaction_a));
}

void wallet_store::representative_set (lumex::store::write_transaction const & transaction_a, lumex::account const & representative_a)
{
	lumex::raw_key rep;
	rep.bytes = representative_a.bytes;
	entry_put_raw (transaction_a, wallet_store::representative_special, lumex::wallet::wallet_value (rep, 0));
}

lumex::account wallet_store::representative (lumex::store::transaction const & transaction_a) const
{
	lumex::wallet::wallet_value value (entry_get_raw (transaction_a, wallet_store::representative_special));
	return reinterpret_cast<lumex::account const &> (value.key);
}

lumex::public_key wallet_store::insert_adhoc (lumex::store::write_transaction const & transaction_a, lumex::raw_key const & prv)
{
	auto cipher = unlock (transaction_a);
	release_assert (cipher, "wallet is locked or password is invalid");
	lumex::public_key pub (lumex::pub_key (prv));
	auto ciphertext = cipher.value ().encrypt (prv, pub.owords[0]);
	entry_put_raw (transaction_a, pub, lumex::wallet::wallet_value (ciphertext, 0));
	return pub;
}

bool wallet_store::insert_watch (lumex::store::write_transaction const & transaction_a, lumex::account const & pub_a)
{
	bool error (!valid_public_key (pub_a));
	if (!error)
	{
		entry_put_raw (transaction_a, pub_a, lumex::wallet::wallet_value (lumex::raw_key (0), 0));
	}
	return error;
}

void wallet_store::erase (lumex::store::write_transaction const & transaction_a, lumex::account const & pub)
{
	backend.entry_del (transaction_a, handle, pub);
}

lumex::wallet::wallet_value wallet_store::entry_get_raw (lumex::store::transaction const & transaction_a, lumex::account const & pub_a) const
{
	auto value = backend.entry_get (transaction_a, handle, pub_a);
	if (value)
	{
		return lumex::wallet::wallet_value{ *value };
	}
	lumex::wallet::wallet_value result;
	result.key.clear ();
	result.work = 0;
	return result;
}

void wallet_store::entry_put_raw (lumex::store::write_transaction const & transaction_a, lumex::account const & pub_a, lumex::wallet::wallet_value const & entry_a)
{
	backend.entry_put (transaction_a, handle, pub_a, entry_a);
}

lumex::wallet::key_type wallet_store::key_type (lumex::wallet::wallet_value const & value_a) const
{
	auto number (value_a.key.number ());
	lumex::wallet::key_type result;
	auto text (number.convert_to<std::string> ());
	if (number > std::numeric_limits<uint64_t>::max ())
	{
		result = key_type::adhoc;
	}
	else
	{
		if ((number >> 32).convert_to<uint32_t> () == 1)
		{
			result = key_type::deterministic;
		}
		else
		{
			result = key_type::unknown;
		}
	}
	return result;
}

lumex::result<lumex::raw_key> wallet_store::fetch (lumex::store::transaction const & transaction, lumex::account const & pub) const
{
	auto cipher = unlock (transaction);
	if (!cipher)
	{
		return lumex::error (lumex::error_common::wallet_locked);
	}

	auto value = entry_get_raw (transaction, pub);
	if (value.key.is_zero ())
	{
		return lumex::error (lumex::error_common::account_not_found_wallet);
	}

	lumex::raw_key prv;
	switch (key_type (value))
	{
		case key_type::deterministic:
		{
			auto seed_l = seed_decrypt (transaction, cipher.value ());
			auto index = static_cast<uint32_t> (value.key.number () & static_cast<uint32_t> (-1));
			prv = lumex::deterministic_key (seed_l, index);
			break;
		}
		case key_type::adhoc:
		{
			prv = cipher.value ().decrypt (value.key, pub.owords[0]);
			break;
		}
		default:
		{
			return lumex::error (lumex::error_common::bad_private_key);
		}
	}

	// Verify the key
	lumex::public_key compare = lumex::pub_key (prv);
	if (pub != compare)
	{
		return lumex::error (lumex::error_common::bad_private_key);
	}

	return prv;
}

bool wallet_store::valid_public_key (lumex::public_key const & pub) const
{
	return pub.number () >= special_count;
}

bool wallet_store::exists (lumex::store::transaction const & transaction_a, lumex::account const & pub) const
{
	return valid_public_key (pub) && find (transaction_a, pub) != end (transaction_a);
}

void wallet_store::serialize_json (lumex::store::transaction const & transaction_a, std::string & string_a) const
{
	boost::property_tree::ptree tree;
	// Iterate from account 0 to include the specials slots in the serialized output.
	for (iterator i{ backend.entries_begin (transaction_a, handle) }, n{ backend.entries_end (transaction_a, handle) }; i != n; ++i)
	{
		tree.put (i->first.to_string (), i->second.key.to_string ());
	}
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

void wallet_store::write_backup (lumex::store::transaction const & transaction_a, std::filesystem::path const & path_a) const
{
	std::ofstream backup_file;
	backup_file.open (path_a.string ());
	if (!backup_file.fail ())
	{
		// Set permissions to 600
		boost::system::error_code ec;
		lumex::set_secure_perm_file (path_a, ec);

		std::string json;
		serialize_json (transaction_a, json);
		backup_file << json;
	}
}

lumex::result<bool> wallet_store::move (lumex::store::write_transaction const & transaction_a, wallet_store & other_a, std::vector<lumex::public_key> const & keys)
{
	if (!valid_password (transaction_a) || !other_a.valid_password (transaction_a))
	{
		return lumex::error (lumex::error_common::wallet_locked);
	}

	bool error = false;
	for (auto i (keys.begin ()), n (keys.end ()); i != n; ++i)
	{
		auto prv_result = other_a.fetch (transaction_a, *i);
		if (prv_result)
		{
			insert_adhoc (transaction_a, prv_result.value ());
			other_a.erase (transaction_a, *i);
		}
		else
		{
			error = true;
		}
	}
	return error;
}

lumex::result<bool> wallet_store::import (lumex::store::write_transaction const & transaction_a, wallet_store & other_a)
{
	if (!valid_password (transaction_a) || !other_a.valid_password (transaction_a))
	{
		return lumex::error (lumex::error_common::wallet_locked);
	}

	bool error = false;
	for (auto i (other_a.begin (transaction_a)), n (other_a.end (transaction_a)); i != n; ++i)
	{
		auto prv_result = other_a.fetch (transaction_a, i->first);
		if (prv_result)
		{
			if (!prv_result.value ().is_zero ())
			{
				insert_adhoc (transaction_a, prv_result.value ());
			}
			else
			{
				insert_watch (transaction_a, i->first);
			}
			other_a.erase (transaction_a, i->first);
		}
		else
		{
			error = true;
		}
	}
	return error;
}

std::optional<uint64_t> wallet_store::work_get (lumex::store::transaction const & transaction, lumex::public_key const & pub) const
{
	auto entry = entry_get_raw (transaction, pub);
	if (!entry.key.is_zero ())
	{
		return entry.work;
	}
	return std::nullopt;
}

void wallet_store::work_put (lumex::store::write_transaction const & transaction_a, lumex::public_key const & pub_a, uint64_t work_a)
{
	auto entry (entry_get_raw (transaction_a, pub_a));
	debug_assert (!entry.key.is_zero ());
	entry.work = work_a;
	entry_put_raw (transaction_a, pub_a, entry);
}

unsigned wallet_store::version (lumex::store::transaction const & transaction_a) const
{
	lumex::wallet::wallet_value value (entry_get_raw (transaction_a, wallet_store::version_special));
	auto entry (value.key);
	auto result (static_cast<unsigned> (entry.bytes[31]));
	return result;
}

void wallet_store::version_put (lumex::store::write_transaction const & transaction_a, unsigned version_a)
{
	lumex::raw_key entry (version_a);
	entry_put_raw (transaction_a, wallet_store::version_special, lumex::wallet::wallet_value (entry, 0));
}

lumex::uint256_union wallet_store::check_value_get (lumex::store::transaction const & transaction_a) const
{
	auto value = entry_get_raw (transaction_a, wallet_store::check_special);
	return value.key;
}

lumex::uint256_union wallet_store::salt_get (lumex::store::transaction const & transaction_a) const
{
	auto value = entry_get_raw (transaction_a, wallet_store::salt_special);
	return value.key;
}

std::optional<wallet_cipher> wallet_store::unlock (lumex::store::transaction const & transaction_a) const
{
	auto const wallet_key_l = wallet_key_decrypt (transaction_a);
	lumex::raw_key zero{};
	zero.clear ();
	lumex::uint256_union check_l{};
	check_l.encrypt (zero, wallet_key_l, salt_get (transaction_a).owords[check_iv_index]);
	if (check_value_get (transaction_a) != check_l)
	{
		return std::nullopt;
	}
	return wallet_cipher{ wallet_key_l };
}

lumex::raw_key wallet_store::wallet_key_decrypt (lumex::store::transaction const & transaction_a) const
{
	lumex::lock_guard<std::recursive_mutex> lock{ mutex };
	lumex::raw_key wallet_l;
	wallet_key_mem.value (wallet_l);
	lumex::raw_key password_l;
	password.value (password_l);
	lumex::raw_key result;
	result.decrypt (wallet_l, password_l, salt_get (transaction_a).owords[0]);
	return result;
}

lumex::raw_key wallet_store::seed (lumex::store::transaction const & transaction) const
{
	auto cipher = unlock (transaction);
	release_assert (cipher, "wallet is locked or password is invalid");
	return seed_decrypt (transaction, cipher.value ());
}

lumex::raw_key wallet_store::seed_decrypt (lumex::store::transaction const & transaction, lumex::wallet::wallet_cipher const & cipher) const
{
	auto encrypted_seed = entry_get_raw (transaction, wallet_store::seed_special).key;
	return cipher.decrypt (encrypted_seed, salt_get (transaction).owords[seed_iv_index]);
}

void wallet_store::seed_set (lumex::store::write_transaction const & transaction_a, lumex::raw_key const & prv_a)
{
	auto cipher = unlock (transaction_a);
	release_assert (cipher, "wallet is locked or password is invalid");
	auto ciphertext = cipher.value ().encrypt (prv_a, salt_get (transaction_a).owords[seed_iv_index]);
	entry_put_raw (transaction_a, wallet_store::seed_special, lumex::wallet::wallet_value (ciphertext, 0));
	deterministic_clear (transaction_a);
}

lumex::public_key wallet_store::deterministic_insert (lumex::store::write_transaction const & transaction_a)
{
	auto index (deterministic_index_get (transaction_a));
	auto prv = deterministic_key (transaction_a, index);
	lumex::public_key result (lumex::pub_key (prv));
	while (exists (transaction_a, result))
	{
		++index;
		prv = deterministic_key (transaction_a, index);
		result = lumex::pub_key (prv);
	}
	uint64_t marker (1);
	marker <<= 32;
	marker |= index;
	entry_put_raw (transaction_a, result, lumex::wallet::wallet_value (marker, 0));
	++index;
	deterministic_index_set (transaction_a, index);
	return result;
}

lumex::public_key wallet_store::deterministic_insert (lumex::store::write_transaction const & transaction_a, uint32_t const index)
{
	auto prv = deterministic_key (transaction_a, index);
	lumex::public_key result (lumex::pub_key (prv));
	uint64_t marker (1);
	marker <<= 32;
	marker |= index;
	entry_put_raw (transaction_a, result, lumex::wallet::wallet_value (marker, 0));
	return result;
}

lumex::raw_key wallet_store::deterministic_key (lumex::store::transaction const & transaction, uint32_t index) const
{
	auto cipher = unlock (transaction);
	release_assert (cipher, "wallet is locked or password is invalid");
	auto wallet_seed = seed_decrypt (transaction, cipher.value ());
	return lumex::deterministic_key (wallet_seed, index);
}

uint32_t wallet_store::deterministic_index_get (lumex::store::transaction const & transaction_a) const
{
	lumex::wallet::wallet_value value (entry_get_raw (transaction_a, wallet_store::deterministic_index_special));
	return static_cast<uint32_t> (value.key.number () & static_cast<uint32_t> (-1));
}

void wallet_store::deterministic_index_set (lumex::store::write_transaction const & transaction_a, uint32_t index_a)
{
	lumex::raw_key index_l (index_a);
	lumex::wallet::wallet_value value (index_l, 0);
	entry_put_raw (transaction_a, wallet_store::deterministic_index_special, value);
}

void wallet_store::deterministic_clear (lumex::store::write_transaction const & transaction_a)
{
	lumex::uint256_union key (0);
	for (auto i (begin (transaction_a)), n (end (transaction_a)); i != n;)
	{
		switch (key_type (lumex::wallet::wallet_value (i->second)))
		{
			case key_type::deterministic:
			{
				auto const & key (i->first);
				erase (transaction_a, key);
				i = begin (transaction_a, key);
				break;
			}
			default:
			{
				++i;
				break;
			}
		}
	}
	deterministic_index_set (transaction_a, 0);
}

bool wallet_store::valid_password (lumex::store::transaction const & transaction_a) const
{
	return unlock (transaction_a).has_value ();
}

bool wallet_store::attempt_password (lumex::store::transaction const & transaction_a, std::string const & password_a)
{
	bool result = false;
	{
		lumex::lock_guard<std::recursive_mutex> lock{ mutex };
		auto password_l = derive_key (transaction_a, password_a);
		password.value_set (password_l);
		result = !valid_password (transaction_a);
	}
	if (!result)
	{
		switch (version (transaction_a))
		{
			case version_4:
				break;
			default:
				debug_assert (false);
		}
	}
	return result;
}

bool wallet_store::rekey (lumex::store::write_transaction const & transaction_a, std::string const & password_a)
{
	lumex::lock_guard<std::recursive_mutex> lock{ mutex };
	auto cipher = unlock (transaction_a);
	if (!cipher)
	{
		return true;
	}
	auto password_new = derive_key (transaction_a, password_a);
	auto encrypted = cipher.value ().reseal (password_new, salt_get (transaction_a).owords[0]);
	password.value_set (password_new);
	wallet_key_mem.value_set (encrypted);
	entry_put_raw (transaction_a, wallet_store::wallet_key_special, lumex::wallet::wallet_value (encrypted, 0));
	return false;
}

lumex::raw_key wallet_store::derive_key (lumex::store::transaction const & transaction_a, std::string const & password_a) const
{
	auto const salt_l = salt_get (transaction_a);
	lumex::raw_key result;
	kdf.phs (result, password_a, salt_l);
	return result;
}

auto wallet_store::begin (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.entries_begin (txn, handle, lumex::account{ special_count }) };
}

auto wallet_store::begin (lumex::store::transaction const & txn, lumex::account const & key) const -> iterator
{
	return iterator{ backend.entries_begin (txn, handle, key) };
}

auto wallet_store::end (lumex::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.entries_end (txn, handle) };
}

auto wallet_store::find (lumex::store::transaction const & txn, lumex::account const & key) const -> iterator
{
	auto it = begin (txn, key);
	auto end_it = end (txn);
	if (it == end_it)
	{
		return end_it;
	}
	if (it->first == key)
	{
		return it;
	}
	return end_it;
}

/*
 * wallet
 */

wallet::wallet (lumex::store::write_transaction & transaction_a, lumex::wallet::wallets & wallets_a, std::string const & wallet_a) :
	lock_observer ([] (bool, bool) {}),
	store (wallets_a.kdf, transaction_a, wallets_a.backend, wallets_a.config.random_representative (), wallets_a.config.password_fanout, wallet_a),
	wallets (wallets_a),
	logger (wallets_a.logger)
{
}

wallet::wallet (lumex::store::write_transaction & transaction_a, lumex::wallet::wallets & wallets_a, std::string const & wallet_a, std::string const & json) :
	lock_observer ([] (bool, bool) {}),
	store (wallets_a.kdf, transaction_a, wallets_a.backend, wallets_a.config.password_fanout, wallet_a, json),
	wallets (wallets_a),
	logger (wallets_a.logger)
{
}

void wallet::enter_initial_password ()
{
	lumex::raw_key password_l;
	{
		lumex::lock_guard<std::recursive_mutex> lock{ store.mutex };
		store.password.value (password_l);
	}
	if (password_l.is_zero ())
	{
		auto transaction (wallets.tx_begin_read ());
		enter_password_impl (transaction, wallet_store::default_password);
	}
}

bool wallet::enter_password (std::string const & password_a)
{
	bool result;
	{
		auto transaction = wallets.tx_begin_write ();
		result = enter_password_impl (transaction, password_a);
	}
	if (!result)
	{
		wallets.refresh_rep_keys_cache ();
	}
	return result;
}

bool wallet::enter_password_impl (lumex::store::transaction const & transaction_a, std::string const & password_a)
{
	auto result (store.attempt_password (transaction_a, password_a));
	if (!result)
	{
		logger.info (lumex::log::type::wallet, "Wallet unlocked");

		auto this_l = shared_from_this ();
		wallets.queue_wallet_action (wallets::high_priority, this_l, [this_l] (wallet & wallet) {
			// Wallets must survive node lifetime
			this_l->search_receivable ();
		});
	}
	else
	{
		logger.warn (lumex::log::type::wallet, "Invalid password, wallet locked");
	}
	lock_observer (result, password_a.empty ());
	return result;
}

lumex::public_key wallet::deterministic_insert_impl (lumex::store::write_transaction const & transaction, bool generate_work)
{
	auto key = store.deterministic_insert (transaction);

	logger.info (lumex::log::type::wallet, "Deterministically inserted new account: {}", key.to_account ());

	if (generate_work)
	{
		work_ensure (key, key);
	}

	if (wallets.check_rep (key))
	{
		logger.info (lumex::log::type::wallet, "New account qualified as a representative: {}", key.to_account ());
		representatives.lock ()->insert (key);
	}

	return key;
}

lumex::public_key wallet::deterministic_insert_impl (lumex::store::write_transaction const & transaction, uint32_t index, bool generate_work)
{
	auto key = store.deterministic_insert (transaction, index);

	logger.info (lumex::log::type::wallet, "Deterministically inserted new account: {} with index: {}", key.to_account (), index);

	if (generate_work)
	{
		work_ensure (key, key);
	}

	if (wallets.check_rep (key))
	{
		logger.info (lumex::log::type::wallet, "New account qualified as a representative: {}", key.to_account ());
		representatives.lock ()->insert (key);
	}

	return key;
}

lumex::result<lumex::public_key> wallet::deterministic_insert (uint32_t index, bool generate_work)
{
	auto transaction = wallets.tx_begin_write ();

	if (!store.valid_password (transaction))
	{
		return lumex::error (lumex::error_common::wallet_locked);
	}

	auto result = deterministic_insert_impl (transaction, index, generate_work);
	transaction.commit ();
	wallets.refresh_rep_keys_cache ();
	return result;
}

lumex::result<lumex::public_key> wallet::deterministic_insert (bool generate_work)
{
	auto transaction = wallets.tx_begin_write ();

	if (!store.valid_password (transaction))
	{
		return lumex::error (lumex::error_common::wallet_locked);
	}

	auto result = deterministic_insert_impl (transaction, generate_work);
	transaction.commit ();
	wallets.refresh_rep_keys_cache ();
	return result;
}

lumex::result<lumex::public_key> wallet::insert_adhoc (lumex::raw_key const & prv, bool generate_work)
{
	auto transaction = wallets.tx_begin_write ();

	if (!store.valid_password (transaction))
	{
		return lumex::error (lumex::error_common::wallet_locked);
	}

	auto key = store.insert_adhoc (transaction, prv);

	logger.info (lumex::log::type::wallet, "Ad-hoc inserted new account: {}", key.to_account ());

	if (generate_work)
	{
		auto ledger_txn = wallets.ledger.tx_begin_read ();
		work_ensure (key, wallets.ledger.latest_root (ledger_txn, key));
	}

	// Makes sure that the representatives container will be in sync with any added keys
	transaction.commit ();

	if (wallets.check_rep (key))
	{
		logger.info (lumex::log::type::wallet, "New account qualified as a representative: {}", key.to_account ());
		representatives.lock ()->insert (key);
		wallets.refresh_rep_keys_cache ();
	}

	return key;
}

bool wallet::insert_watch (lumex::public_key const & pub_a)
{
	auto transaction = wallets.tx_begin_write ();
	return insert_watch_impl (transaction, pub_a);
}

bool wallet::insert_watch_impl (lumex::store::write_transaction const & transaction_a, lumex::public_key const & pub_a)
{
	return store.insert_watch (transaction_a, pub_a);
}

bool wallet::exists (lumex::public_key const & account_a)
{
	auto transaction (wallets.tx_begin_read ());
	return store.exists (transaction, account_a);
}

bool wallet::import (std::string const & json_a, std::string const & password_a)
{
	bool error (true);
	auto transaction (wallets.tx_begin_write ());
	lumex::uint256_union id;
	random_pool::generate_block (id.bytes.data (), id.bytes.size ());
	try
	{
		auto temp = std::make_unique<wallet_store> (wallets.kdf, transaction, wallets.backend, 1, id.to_string (), json_a);
		if (!temp->attempt_password (transaction, password_a))
		{
			auto result = store.import (transaction, *temp);
			error = !result || result.value ();
		}
		temp->destroy (transaction);
	}
	catch (std::exception const & ex)
	{
		logger.error (lumex::log::type::wallet, "Failed to import wallet: {}", ex.what ());
	}
	return error;
}

void wallet::serialize_json (std::string & json_a)
{
	auto transaction (wallets.tx_begin_read ());
	store.serialize_json (transaction, json_a);
}

void wallet::write_backup (std::filesystem::path const & path_a)
{
	auto transaction (wallets.tx_begin_read ());
	store.write_backup (transaction, path_a);
}

std::shared_ptr<lumex::block> wallet::receive_action (lumex::block_hash const & send_hash_a, lumex::account const & representative_a, lumex::uint128_union const & amount_a, lumex::account const & account_a, uint64_t work_a, bool generate_work_a)
{
	std::shared_ptr<lumex::block> block;
	lumex::block_details details;
	details.is_receive = true;
	if (wallets.config.receive_minimum.number () <= amount_a.number ())
	{
		auto ledger_txn = wallets.ledger.tx_begin_read ();
		auto transaction (wallets.tx_begin_read ());
		if (wallets.ledger.any.block_exists_or_pruned (ledger_txn, send_hash_a))
		{
			auto pending_info = wallets.ledger.any.pending_get (ledger_txn, lumex::pending_key (account_a, send_hash_a));
			if (pending_info)
			{
				auto prv_result = store.fetch (transaction, account_a);
				if (prv_result)
				{
					logger.info (lumex::log::type::wallet, "Receiving block: {} from account: {}, amount: {} raw",
					send_hash_a,
					account_a,
					lumex::log::as_raw_lumex (pending_info->amount));

					if (work_a == 0)
					{
						work_a = store.work_get (transaction, account_a).value_or (0);
					}
					auto info = wallets.ledger.any.account_get (ledger_txn, account_a);
					if (info)
					{
						block = std::make_shared<lumex::state_block> (account_a, info->head, info->representative, info->balance.number () + pending_info->amount.number (), send_hash_a, prv_result.value (), account_a, work_a);
						details.epoch = std::max (info->epoch (), pending_info->epoch);
					}
					else
					{
						block = std::make_shared<lumex::state_block> (account_a, 0, representative_a, pending_info->amount, reinterpret_cast<lumex::link const &> (send_hash_a), prv_result.value (), account_a, work_a);
						details.epoch = pending_info->epoch;
					}
				}
				else
				{
					logger.warn (lumex::log::type::wallet, "Unable to receive, wallet locked, block: {} to account: {}",
					send_hash_a,
					account_a);
				}
			}
			else
			{
				// Ledger doesn't have this marked as available to receive anymore
				logger.warn (lumex::log::type::wallet, "Not receiving block: {}, block already received", send_hash_a);
			}
		}
		else
		{
			// Ledger doesn't have this block anymore.
			logger.warn (lumex::log::type::wallet, "Not receiving block: {}, block no longer exists or pruned", send_hash_a);
		}
	}
	else
	{
		// Someone sent us something below the threshold of receiving
		logger.warn (lumex::log::type::wallet, "Not receiving block: {} due to minimum receive threshold", send_hash_a);
	}
	if (block != nullptr)
	{
		if (action_complete (block, account_a, generate_work_a, details))
		{
			// Return null block after work generation or ledger process error
			block = nullptr;
		}
	}
	return block;
}

std::shared_ptr<lumex::block> wallet::change_action (lumex::account const & source_a, lumex::account const & representative_a, uint64_t work_a, bool generate_work_a)
{
	std::shared_ptr<lumex::block> block;
	lumex::block_details details;
	{
		auto transaction (wallets.tx_begin_read ());
		auto ledger_txn = wallets.ledger.tx_begin_read ();
		if (store.valid_password (transaction))
		{
			auto existing (store.find (transaction, source_a));
			if (existing != store.end (transaction) && !wallets.ledger.any.account_head (ledger_txn, source_a).is_zero ())
			{
				logger.info (lumex::log::type::wallet, "Changing representative for account: {} to: {}",
				source_a,
				representative_a);

				auto info = wallets.ledger.any.account_get (ledger_txn, source_a);
				release_assert (info, "could not find account info for account in wallet change_action", source_a.to_account ());
				auto prv_result = store.fetch (transaction, source_a);
				release_assert (prv_result, "failed to fetch private key for account in wallet change_action", source_a.to_account ());
				if (work_a == 0)
				{
					work_a = store.work_get (transaction, source_a).value_or (0);
				}
				block = std::make_shared<lumex::state_block> (source_a, info->head, representative_a, info->balance, 0, prv_result.value (), source_a, work_a);
				details.epoch = info->epoch ();
			}
			else
			{
				logger.warn (lumex::log::type::wallet, "Changing representative for account: {} failed, wallet locked or account not found", source_a);
			}
		}
		else
		{
			logger.warn (lumex::log::type::wallet, "Changing representative for account: {} failed, wallet locked", source_a);
		}
	}
	if (block != nullptr)
	{
		if (action_complete (block, source_a, generate_work_a, details))
		{
			// Return null block after work generation or ledger process error
			block = nullptr;
		}
	}
	return block;
}

std::shared_ptr<lumex::block> wallet::send_action (lumex::account const & source_a, lumex::account const & account_a, lumex::uint128_t const & amount_a, uint64_t work_a, bool generate_work_a, std::optional<std::string> id_a)
{
	auto prepare_send = [this, &wallets = this->wallets, &store = this->store, &source_a, &amount_a, &work_a, &account_a, &id_a] (auto const & transaction) {
		auto ledger_txn = wallets.ledger.tx_begin_read ();
		auto error (false);
		auto cached_block (false);
		std::shared_ptr<lumex::block> block;
		lumex::block_details details;
		details.is_send = true;
		if (id_a)
		{
			auto existing_value = wallets.backend.send_action_id_get (transaction, *id_a);
			if (existing_value)
			{
				auto existing_hash = static_cast<lumex::block_hash> (*existing_value);
				block = wallets.ledger.any.block_get (ledger_txn, existing_hash);
				if (block != nullptr)
				{
					logger.warn (lumex::log::type::wallet, "Block already exists for send action with id: {}, existing hash: {}",
					id_a.value (),
					existing_hash);

					cached_block = true;
					wallets.network.flood_block (block, lumex::transport::traffic_type::block_broadcast_initial);
				}
				else
				{
					logger.warn (lumex::log::type::wallet, "Block was not found in ledger for send action with id: {}, hash: {}",
					id_a.value (),
					existing_hash);
				}
			}
		}
		if (!error && block == nullptr)
		{
			if (store.valid_password (transaction))
			{
				auto existing (store.find (transaction, source_a));
				if (existing != store.end (transaction))
				{
					auto balance (wallets.ledger.any.account_balance (ledger_txn, source_a));
					if (balance && balance.value ().number () >= amount_a)
					{
						logger.info (lumex::log::type::wallet, "Sending from account: {} to: {}, amount: {} raw",
						source_a,
						account_a,
						lumex::log::as_raw_lumex (amount_a));

						auto info = wallets.ledger.any.account_get (ledger_txn, source_a);
						release_assert (info, "could not find account info for account in wallet send_action", source_a.to_account ());
						auto prv_result = store.fetch (transaction, source_a);
						release_assert (prv_result, "failed to fetch private key for account in wallet send_action", source_a.to_account ());
						if (work_a == 0)
						{
							work_a = store.work_get (transaction, source_a).value_or (0);
						}
						block = std::make_shared<lumex::state_block> (source_a, info->head, info->representative, balance.value ().number () - amount_a, account_a, prv_result.value (), source_a, work_a);
						details.epoch = info->epoch ();
						if (id_a && block != nullptr)
						{
							// `id_a` being set implies the caller passed a write transaction (see below).
							// `if constexpr` keeps the put out of the read-txn instantiation of this lambda.
							if constexpr (std::is_same_v<std::decay_t<decltype (transaction)>, lumex::store::write_transaction>)
							{
								if (!wallets.backend.send_action_id_put (transaction, *id_a, block->hash ()))
								{
									block = nullptr;
									error = true;
								}
							}
							else
							{
								release_assert (false, "send_action with id requires a write transaction");
							}
						}
					}
					else
					{
						if (balance)
						{
							logger.warn (lumex::log::type::wallet, "Insufficient balance for send from: {}, required: {} raw, available: {} raw",
							source_a,
							lumex::log::as_raw_lumex (amount_a),
							lumex::log::as_raw_lumex (balance.value ()));
						}
						else
						{
							logger.warn (lumex::log::type::wallet, "Insufficient balance for send from: {}, required: {} raw, available: unknown",
							source_a,
							lumex::log::as_raw_lumex (amount_a));
						}
					}
				}
			}
		}
		return std::make_tuple (block, error, cached_block, details);
	};

	std::tuple<std::shared_ptr<lumex::block>, bool, bool, lumex::block_details> result;
	{
		if (id_a)
		{
			result = prepare_send (wallets.tx_begin_write ());
		}
		else
		{
			result = prepare_send (wallets.tx_begin_read ());
		}
	}

	std::shared_ptr<lumex::block> block;
	bool error;
	bool cached_block;
	lumex::block_details details;
	std::tie (block, error, cached_block, details) = result;

	if (!error && block != nullptr && !cached_block)
	{
		if (action_complete (block, source_a, generate_work_a, details))
		{
			// Return null block after work generation or ledger process error
			block = nullptr;
		}
	}
	return block;
}

bool wallet::action_complete (std::shared_ptr<lumex::block> const & block_a, lumex::account const & account_a, bool const generate_work_a, lumex::block_details const & details_a)
{
	bool error{ false };
	// Unschedule any work caching for this account
	wallets.delayed_work->erase (account_a);
	if (block_a != nullptr)
	{
		auto required_difficulty{ wallets.network_params.work.threshold (block_a->work_version (), details_a) };
		if (wallets.network_params.work.difficulty (*block_a) < required_difficulty)
		{
			logger.info (lumex::log::type::wallet, "Cached or provided work for block: {}, account {}: is invalid, regenerating...",
			block_a->hash (),
			account_a);

			debug_assert (required_difficulty <= wallets.node.max_work_generate_difficulty (block_a->work_version ()));
			error = !wallets.node.work_generate_blocking (*block_a, required_difficulty).has_value ();
		}
		if (!error)
		{
			auto result = wallets.node.process_local (block_a);
			error = !result || result.value () != lumex::block_status::progress;
			debug_assert (error || block_a->sideband ().details == details_a);
		}
		if (!error && generate_work_a)
		{
			// Pregenerate work for next block based on the block just created
			work_ensure (account_a, block_a->hash ());
		}
	}
	return error;
}

bool wallet::change_sync (lumex::account const & source_a, lumex::account const & representative_a)
{
	std::promise<bool> result;
	std::future<bool> future = result.get_future ();
	change_async (
	source_a, representative_a, [&result] (std::shared_ptr<lumex::block> const & block_a) {
		result.set_value (block_a == nullptr);
	},
	true);
	return future.get ();
}

void wallet::change_async (lumex::account const & source_a, lumex::account const & representative_a, std::function<void (std::shared_ptr<lumex::block> const &)> const & action_a, uint64_t work_a, bool generate_work_a)
{
	auto this_l (shared_from_this ());
	wallets.queue_wallet_action (wallets::high_priority, this_l, [this_l, source_a, representative_a, action_a, work_a, generate_work_a] (wallet & wallet_a) {
		auto block (wallet_a.change_action (source_a, representative_a, work_a, generate_work_a));
		action_a (block);
	});
}

bool wallet::receive_sync (std::shared_ptr<lumex::block> const & block_a, lumex::account const & representative_a, lumex::uint128_t const & amount_a)
{
	std::promise<bool> result;
	std::future<bool> future = result.get_future ();
	receive_async (
	block_a->hash (), representative_a, amount_a, block_a->destination (), [&result] (std::shared_ptr<lumex::block> const & block_a) {
		result.set_value (block_a == nullptr);
	},
	true);
	return future.get ();
}

void wallet::receive_async (lumex::block_hash const & hash_a, lumex::account const & representative_a, lumex::uint128_t const & amount_a, lumex::account const & account_a, std::function<void (std::shared_ptr<lumex::block> const &)> const & action_a, uint64_t work_a, bool generate_work_a)
{
	auto this_l (shared_from_this ());
	wallets.queue_wallet_action (amount_a, this_l, [this_l, hash_a, representative_a, amount_a, account_a, action_a, work_a, generate_work_a] (wallet & wallet_a) {
		auto block (wallet_a.receive_action (hash_a, representative_a, amount_a, account_a, work_a, generate_work_a));
		action_a (block);
	});
}

lumex::block_hash wallet::send_sync (lumex::account const & source_a, lumex::account const & account_a, lumex::uint128_t const & amount_a)
{
	std::promise<lumex::block_hash> result;
	std::future<lumex::block_hash> future = result.get_future ();
	send_async (
	source_a, account_a, amount_a, [&result] (std::shared_ptr<lumex::block> const & block_a) {
		result.set_value (block_a->hash ());
	},
	true);
	return future.get ();
}

void wallet::send_async (lumex::account const & source_a, lumex::account const & account_a, lumex::uint128_t const & amount_a, std::function<void (std::shared_ptr<lumex::block> const &)> const & action_a, uint64_t work_a, bool generate_work_a, std::optional<std::string> id_a)
{
	auto this_l (shared_from_this ());
	wallets.queue_wallet_action (wallets::high_priority, this_l, [this_l, source_a, account_a, amount_a, action_a, work_a, generate_work_a, id_a] (wallet & wallet_a) {
		auto block (wallet_a.send_action (source_a, account_a, amount_a, work_a, generate_work_a, id_a));
		action_a (block);
	});
}

// Update work for account if latest root is root_a
void wallet::work_update_impl (lumex::store::write_transaction const & transaction_a, lumex::account const & account_a, lumex::root const & root_a, uint64_t work_a)
{
	debug_assert (!wallets.network_params.work.validate_entry (lumex::work_version::work_1, root_a, work_a));
	debug_assert (store.exists (transaction_a, account_a));
	auto ledger_txn = wallets.ledger.tx_begin_read ();
	auto latest (wallets.ledger.latest_root (ledger_txn, account_a));
	if (latest == root_a)
	{
		store.work_put (transaction_a, account_a, work_a);
	}
	else
	{
		logger.warn (lumex::log::type::wallet, "Cached work no longer valid, discarding");
	}
}

void wallet::work_ensure (lumex::account const & account_a, lumex::root const & root_a)
{
	using namespace std::chrono_literals;
	std::chrono::seconds const precache_delay = wallets.network_params.network.is_dev_network () ? 1s : 10s;

	wallets.delayed_work->operator[] (account_a) = root_a;

	wallets.workers.post_delayed (precache_delay, [this_l = shared_from_this (), account_a, root_a] {
		if (this_l->wallets.stopped)
		{
			return;
		}
		auto delayed_work = this_l->wallets.delayed_work.lock ();
		auto existing (delayed_work->find (account_a));
		if (existing != delayed_work->end () && existing->second == root_a)
		{
			delayed_work->erase (existing);
			this_l->wallets.queue_wallet_action (wallets::generate_priority, this_l, [account_a, root_a] (wallet & wallet_a) {
				wallet_a.work_cache_blocking (account_a, root_a);
			});
		}
	});
}

bool wallet::search_receivable ()
{
	auto transaction = wallets.tx_begin_read ();
	return search_receivable_impl (transaction);
}

bool wallet::search_receivable_impl (lumex::store::transaction const & wallet_transaction_a)
{
	auto result (!store.valid_password (wallet_transaction_a));
	if (!result)
	{
		logger.debug (lumex::log::type::wallet, "Beginning receivable block search");

		for (auto i (store.begin (wallet_transaction_a)), n (store.end (wallet_transaction_a)); i != n; ++i)
		{
			auto ledger_txn = wallets.ledger.tx_begin_read ();
			lumex::account const & account (i->first);
			// Don't search pending for watch-only accounts
			if (!lumex::wallet::wallet_value (i->second).key.is_zero ())
			{
				for (auto j (wallets.ledger.store.pending.begin (ledger_txn, lumex::pending_key (account, 0))), k (wallets.ledger.store.pending.end (ledger_txn)); j != k && lumex::pending_key (j->first).account == account; ++j)
				{
					lumex::pending_key key (j->first);
					auto hash (key.hash);
					lumex::pending_info pending (j->second);
					auto amount (pending.amount.number ());
					if (wallets.config.receive_minimum.number () <= amount)
					{
						bool const confirmed = wallets.ledger.cemented.block_exists_or_pruned (ledger_txn, hash);

						logger.info (lumex::log::type::wallet, "Found a receivable block: {} ({}) for account: {} from: {}",
						hash,
						confirmed ? "confirmed" : "unconfirmed",
						key.account,
						pending.source);

						if (confirmed)
						{
							auto representative = store.representative (wallet_transaction_a);
							// Receive confirmed block
							receive_async (hash, representative, amount, account, [] (std::shared_ptr<lumex::block> const &) {});
						}
						else if (!wallets.node.cementing_set.contains (hash))
						{
							auto block = wallets.ledger.any.block_get (ledger_txn, hash);
							if (block)
							{
								// Request confirmation for block which is not being processed yet
								wallets.node.start_election (block);
							}
						}
					}
				}
			}
		}

		logger.debug (lumex::log::type::wallet, "Receivable block search phase complete");
	}
	else
	{
		logger.warn (lumex::log::type::wallet, "Unable to search receivable blocks, wallet is locked. Blocks won't be auto-received until the wallet is unlocked");
	}
	return result;
}

void wallet::init_free_accounts_impl (lumex::store::transaction const & transaction_a)
{
	free_accounts.clear ();
	for (auto i (store.begin (transaction_a)), n (store.end (transaction_a)); i != n; ++i)
	{
		free_accounts.insert (i->first);
	}
}

uint32_t wallet::deterministic_check (uint32_t index)
{
	auto transaction = wallets.tx_begin_read ();
	return deterministic_check_impl (transaction, index);
}

uint32_t wallet::deterministic_check_impl (lumex::store::transaction const & transaction_a, uint32_t index)
{
	auto ledger_txn = wallets.ledger.tx_begin_read ();
	for (uint32_t i (index + 1), n (index + 64); i < n; ++i)
	{
		auto prv = store.deterministic_key (transaction_a, i);
		lumex::keypair pair (prv.to_string ());
		// Check if account received at least 1 block
		auto latest (wallets.ledger.any.account_head (ledger_txn, pair.pub));
		if (!latest.is_zero ())
		{
			index = i;
			// i + 64 - Check additional 64 accounts
			// i/64 - Check additional accounts for large wallets. I.e. 64000/64 = 1000 accounts to check
			n = i + 64 + (i / 64);
		}
		else
		{
			// Check if there are pending blocks for account
			auto current = wallets.ledger.any.receivable_upper_bound (ledger_txn, pair.pub, 0);
			if (current != wallets.ledger.any.receivable_end ())
			{
				index = i;
				n = i + 64 + (i / 64);
			}
		}
	}
	return index;
}

lumex::public_key wallet::change_seed (lumex::raw_key const & prv_a, uint32_t count)
{
	lumex::public_key result;
	{
		auto transaction = wallets.tx_begin_write ();
		result = change_seed_impl (transaction, prv_a, count);
	}
	wallets.refresh_rep_keys_cache ();
	return result;
}

lumex::public_key wallet::change_seed_impl (lumex::store::write_transaction const & transaction_a, lumex::raw_key const & prv_a, uint32_t count)
{
	logger.info (lumex::log::type::wallet, "Changing wallet seed");

	store.seed_set (transaction_a, prv_a);
	auto account = deterministic_insert_impl (transaction_a);
	if (count == 0)
	{
		count = deterministic_check_impl (transaction_a, 0);
		logger.info (lumex::log::type::wallet, "Auto-detected {} accounts to generate from seed", count);
	}
	for (uint32_t i (0); i < count; ++i)
	{
		// Disable work generation to prevent weak CPU nodes stuck
		account = deterministic_insert_impl (transaction_a, false);
	}

	logger.info (lumex::log::type::wallet, "Completed changing wallet seed and generating accounts");

	return account;
}

void wallet::deterministic_restore ()
{
	{
		auto transaction = wallets.tx_begin_write ();
		deterministic_restore_impl (transaction);
	}
	wallets.refresh_rep_keys_cache ();
}

void wallet::deterministic_restore_impl (lumex::store::write_transaction const & transaction_a)
{
	auto index (store.deterministic_index_get (transaction_a));
	auto new_index (deterministic_check_impl (transaction_a, index));
	for (uint32_t i (index); i <= new_index && index != new_index; ++i)
	{
		// Disable work generation to prevent weak CPU nodes stuck
		deterministic_insert_impl (transaction_a, false);
	}
}

bool wallet::rekey (std::string const & password_a)
{
	bool result;
	{
		auto transaction = wallets.tx_begin_write ();
		result = store.rekey (transaction, password_a);
	}
	if (!result)
	{
		wallets.refresh_rep_keys_cache ();
	}
	return result;
}

bool wallet::is_locked () const
{
	auto transaction = wallets.tx_begin_read ();
	return !store.valid_password (transaction);
}

void wallet::lock ()
{
	logger.info (lumex::log::type::wallet, "Wallet locked");
	lumex::raw_key empty;
	empty.clear ();
	store.password.value_set (empty);
	wallets.refresh_rep_keys_cache ();
}

void wallet::remove_account (lumex::account const & account_a)
{
	{
		auto transaction = wallets.tx_begin_write ();
		store.erase (transaction, account_a);
	}
	wallets.refresh_rep_keys_cache ();
}

std::vector<lumex::account> wallet::accounts () const
{
	auto transaction = wallets.tx_begin_read ();
	return store.accounts (transaction);
}

lumex::result<bool> wallet::move_accounts (wallet & source, std::vector<lumex::public_key> const & accounts)
{
	lumex::result<bool> result{ true };
	{
		auto transaction = wallets.tx_begin_write ();
		result = store.move (transaction, source.store, accounts);
	}
	wallets.refresh_rep_keys_cache ();
	return result;
}

key_type wallet::key_type (lumex::account const & account_a) const
{
	auto transaction = wallets.tx_begin_read ();
	auto value = store.entry_get_raw (transaction, account_a);
	return store.key_type (value);
}

void wallet::set_representative (lumex::account const & rep_a)
{
	auto transaction = wallets.tx_begin_write ();
	store.representative_set (transaction, rep_a);
}

lumex::account wallet::get_representative () const
{
	auto transaction = wallets.tx_begin_read ();
	return store.representative (transaction);
}

lumex::result<lumex::raw_key> wallet::get_seed () const
{
	auto transaction = wallets.tx_begin_read ();
	if (!store.valid_password (transaction))
	{
		return lumex::error (lumex::error_common::wallet_locked);
	}
	return store.seed (transaction);
}

uint32_t wallet::get_deterministic_index () const
{
	auto transaction = wallets.tx_begin_read ();
	return store.deterministic_index_get (transaction);
}

lumex::result<uint64_t> wallet::get_work (lumex::public_key const & pub) const
{
	auto transaction = wallets.tx_begin_read ();
	auto result = store.work_get (transaction, pub);
	if (result)
	{
		return *result;
	}
	return lumex::error (lumex::error_common::account_not_found_wallet);
}

void wallet::set_work (lumex::public_key const & pub_a, uint64_t work_a)
{
	auto transaction = wallets.tx_begin_write ();
	store.work_put (transaction, pub_a, work_a);
}

lumex::result<lumex::raw_key> wallet::fetch_prv (lumex::account const & pub_a) const
{
	auto transaction = wallets.tx_begin_read ();
	return store.fetch (transaction, pub_a);
}

bool wallet::live ()
{
	return store.handle->valid ();
}

std::unordered_set<lumex::account> wallet::reps () const
{
	return *representatives.lock ();
}

void wallet::work_cache_blocking (lumex::account const & account_a, lumex::root const & root_a)
{
	if (wallets.node.work_generation_enabled ())
	{
		auto difficulty (wallets.node.default_difficulty (lumex::work_version::work_1));
		auto opt_work_l (wallets.node.work_generate_blocking (lumex::work_version::work_1, root_a, difficulty, account_a));
		if (opt_work_l.has_value ())
		{
			auto transaction_l (wallets.tx_begin_write ());
			// No TOCTOU between `live()` and the ops below: LMDB's single-writer rule means
			// `wallets::destroy()` cannot commit (and clear `store.handle`) until `transaction_l`
			// ends, so the handle is stable for the duration of this block.
			if (live () && store.exists (transaction_l, account_a))
			{
				work_update_impl (transaction_l, account_a, root_a, opt_work_l.value ());
			}
		}
		else if (!wallets.node.stopped)
		{
			logger.warn (lumex::log::type::wallet, "Could not precache work for root: {} due to work generation failure", root_a);
		}
	}
}

/*
 * wallets
 */

lumex::uint128_t const wallets::generate_priority = std::numeric_limits<lumex::uint128_t>::max ();
lumex::uint128_t const wallets::high_priority = std::numeric_limits<lumex::uint128_t>::max () - 1;

wallets::wallets (
lumex::node & node_a,
lumex::wallet::wallets_backend & backend_a,
lumex::ledger & ledger_a,
lumex::node_config const & config_a,
lumex::network_params const & network_params_a,
lumex::online_reps & online_reps_a,
lumex::network & network_a,
lumex::stats & stats_a,
lumex::logger & logger_a) :
	node{ node_a },
	backend{ backend_a },
	ledger{ ledger_a },
	config{ config_a },
	network_params{ network_params_a },
	online_reps{ online_reps_a },
	network{ network_a },
	stats{ stats_a },
	logger{ logger_a },
	observer{ [] (bool) {} },
	kdf{ network_params.kdf_work },
	workers{ config.wallet_threads, lumex::thread_role::name::wallet_worker, /* auto_start */ true }
{
	logger.info (lumex::log::type::wallet, "Loading wallets from: {}", backend.database_path ().string ());

	lumex::unique_lock<lumex::mutex> lock{ mutex };
	{
		auto transaction (tx_begin_write ());
		for (auto it = backend.index_begin (transaction), end = backend.index_end (transaction); it != end; ++it)
		{
			// The wallet index range may also include entries for non-wallet sub-tables (e.g. `send_action_ids` on LMDB);
			// skip anything that doesn't parse as a 64-char hex wallet id.
			auto id = try_parse_wallet_id (bytes_to_string (it->first));
			if (!id)
			{
				continue;
			}
			auto text = id->to_string ();
			release_assert (items.find (*id) == items.end ());
			try
			{
				auto wallet_l = std::make_shared<wallet> (transaction, *this, text);
				items[*id] = wallet_l;
			}
			catch (std::exception const & ex)
			{
				logger.error (lumex::log::type::wallet, "Failed to open wallet {}: {}", text, ex.what ());
			}
		}
	}

	logger.info (lumex::log::type::wallet, "Found {} wallet(s)", items.size ());
	for (auto const & item : items)
	{
		logger.info (lumex::log::type::wallet, "Wallet: {}", item.first);
	}

	// Backup before upgrade wallets
	bool backup_required (false);
	if (config.backup_before_upgrade)
	{
		auto transaction (tx_begin_read ());
		for (auto & item : items)
		{
			if (item.second->store.version (transaction) != wallet_store::version_current)
			{
				backup_required = true;
				break;
			}
		}
	}
	if (backup_required)
	{
		backend.backup (logger);
	}
	for (auto & item : items)
	{
		item.second->enter_initial_password ();
	}
}

wallets::~wallets ()
{
	stop ();
}

void wallets::start ()
{
	thread = std::thread{ [this] () {
		lumex::thread_role::set (lumex::thread_role::name::wallet_actions);
		do_wallet_actions ();
	} };

	if (config.enable_voting)
	{
		reps_thread = std::thread{ [this] () {
			lumex::thread_role::set (lumex::thread_role::name::wallet_reps);
			run_reps_scan ();
		} };
	}

	if (!node.flags.disable_search_pending)
	{
		receivable_thread = std::thread{ [this] () {
			lumex::thread_role::set (lumex::thread_role::name::wallet_receivable);
			run_receivable_scan ();
		} };
	}
}

void wallets::stop ()
{
	{
		lumex::lock_guard<lumex::mutex> action_lock{ action_mutex };
		stopped = true;
		actions.clear ();
	}
	condition.notify_all ();
	reps_condition.notify_all ();
	receivable_condition.notify_all ();

	if (thread.joinable ())
	{
		thread.join ();
	}
	if (reps_thread.joinable ())
	{
		reps_thread.join ();
	}
	if (receivable_thread.joinable ())
	{
		receivable_thread.join ();
	}

	workers.stop ();
}

std::shared_ptr<wallet> wallets::open (lumex::wallet_id const & id_a)
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	std::shared_ptr<wallet> result;
	auto existing (items.find (id_a));
	if (existing != items.end ())
	{
		result = existing->second;
	}
	return result;
}

std::shared_ptr<wallet> wallets::create (lumex::wallet_id const & id_a)
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	debug_assert (items.find (id_a) == items.end ());
	{
		auto transaction (tx_begin_write ());
		try
		{
			auto result = std::make_shared<wallet> (transaction, *this, id_a.to_string ());
			debug_assert (result->store.valid_password (transaction));
			items[id_a] = result;
			return result;
		}
		catch (std::exception const & ex)
		{
			logger.error (lumex::log::type::wallet, "Failed to create wallet {}: {}", id_a, ex.what ());
		}
	}
	return nullptr;
}

std::shared_ptr<wallet> wallets::create_from_json (lumex::wallet_id const & id_a, std::string const & json_a)
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	debug_assert (items.find (id_a) == items.end ());
	{
		auto transaction (tx_begin_write ());
		try
		{
			auto result = std::make_shared<wallet> (transaction, *this, id_a.to_string (), json_a);
			items[id_a] = result;
			return result;
		}
		catch (std::exception const & ex)
		{
			logger.error (lumex::log::type::wallet, "Failed to create wallet {} from JSON: {}", id_a, ex.what ());
		}
	}
	return nullptr;
}

bool wallets::search_receivable (lumex::wallet_id const & wallet_a)
{
	auto result (false);
	if (auto wallet = open (wallet_a); wallet != nullptr)
	{
		result = wallet->search_receivable ();
	}
	return result;
}

void wallets::search_receivable_all ()
{
	auto wallets_l = all_wallets ();
	for (auto const & [id, wallet] : wallets_l)
	{
		wallet->search_receivable ();
	}
}

void wallets::destroy (lumex::wallet_id const & id_a)
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	auto transaction (tx_begin_write ());
	// action_mutex should be after transactions to prevent deadlocks in deterministic_insert () & insert_adhoc ()
	lumex::lock_guard<lumex::mutex> action_lock{ action_mutex };
	auto existing (items.find (id_a));
	release_assert (existing != items.end ());
	auto wallet (existing->second);
	items.erase (existing);
	wallet->store.destroy (transaction);
}

void wallets::reload ()
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	auto transaction (tx_begin_write ());
	std::unordered_set<lumex::uint256_union> stored_items;
	for (auto it = backend.index_begin (transaction), end = backend.index_end (transaction); it != end; ++it)
	{
		// The wallet index range may also include entries for non-wallet sub-tables (e.g. `send_action_ids` on LMDB);
		// skip anything that doesn't parse as a 64-char hex wallet id.
		auto id = try_parse_wallet_id (bytes_to_string (it->first));
		if (!id)
		{
			continue;
		}
		auto text = id->to_string ();
		// New wallet
		if (items.find (*id) == items.end ())
		{
			try
			{
				auto wallet_l = std::make_shared<wallet> (transaction, *this, text);
				items[*id] = wallet_l;
			}
			catch (std::exception const & ex)
			{
				logger.error (lumex::log::type::wallet, "Failed to open wallet {}: {}", text, ex.what ());
			}
		}
		// List of wallets on disk
		stored_items.insert (*id);
	}
	// Delete non existing wallets from memory
	std::vector<lumex::wallet_id> deleted_items;
	for (auto i : items)
	{
		if (stored_items.find (i.first) == stored_items.end ())
		{
			deleted_items.push_back (i.first);
		}
	}
	for (auto & i : deleted_items)
	{
		debug_assert (items.find (i) == items.end ());
		items.erase (i);
	}
}

void wallets::queue_wallet_action (lumex::uint128_t const & amount_a, std::shared_ptr<wallet> const & wallet_a, std::function<void (wallet &)> action_a)
{
	{
		lumex::lock_guard<lumex::mutex> action_lock{ action_mutex };
		actions.emplace (amount_a, std::make_pair (wallet_a, action_a));
	}
	condition.notify_all ();
}

bool wallets::exists_impl (lumex::store::transaction const & transaction_a, lumex::account const & account_a)
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	auto result (false);
	for (auto i (items.begin ()), n (items.end ()); !result && i != n; ++i)
	{
		result = i->second->store.exists (transaction_a, account_a);
	}
	return result;
}

bool wallets::exists (lumex::account const & account_a)
{
	auto transaction (tx_begin_read ());
	return exists_impl (transaction, account_a);
}

bool wallets::exists_any (lumex::account const & account1, lumex::account const & account2)
{
	auto transaction (tx_begin_read ());
	return exists_impl (transaction, account1) || exists_impl (transaction, account2);
}

lumex::store::write_transaction wallets::tx_begin_write ()
{
	return backend.tx_begin_write ();
}

lumex::store::read_transaction wallets::tx_begin_read ()
{
	return backend.tx_begin_read ();
}

void wallets::clear_send_ids ()
{
	auto transaction (tx_begin_write ());
	backend.send_action_ids_clear (transaction);
}

wallet_representatives wallets::reps () const
{
	return *representatives.lock ();
}

auto wallets::signer () -> signer_t
{
	return [this] (auto const & callback) { foreach_representative (callback); };
}

bool wallets::check_rep (lumex::account const & account)
{
	auto half_principal_weight = node.minimum_principal_weight () / 2;
	auto representatives_locked = representatives.lock ();
	return check_rep_impl (*representatives_locked, account, half_principal_weight);
}

bool wallets::check_rep_impl (wallet_representatives & reps, lumex::account const & account, lumex::uint128_t const & half_principal_weight)
{
	auto weight = ledger.weight (account);
	if (weight < config.vote_minimum.number ())
	{
		return false; // account not a representative
	}

	if (weight >= half_principal_weight)
	{
		reps.half_principal = true;
	}

	auto insert_result = reps.accounts.insert (account);
	if (!insert_result.second)
	{
		return false; // account already exists
	}

	++reps.voting;

	return true;
}

void wallets::refresh_reps ()
{
	refresh_rep_index ();
	refresh_rep_keys_cache ();
}

void wallets::refresh_rep_index ()
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	auto reps_locked = representatives.lock ();
	reps_locked->clear ();

	auto const half_principal_weight = node.minimum_principal_weight () / 2;

	auto wallet_txn = tx_begin_read ();

	for (auto const & [id, wallet] : items)
	{
		std::unordered_set<lumex::account> new_representatives;
		for (auto i = wallet->store.begin (wallet_txn), n = wallet->store.end (wallet_txn); i != n; ++i)
		{
			auto account = i->first;
			if (check_rep_impl (*reps_locked, account, half_principal_weight))
			{
				new_representatives.insert (account);
			}
		}
		wallet->representatives.lock ()->swap (new_representatives);
	}
}

void wallets::foreach_representative (std::function<void (lumex::public_key const & pub, lumex::raw_key const & prv)> const & action)
{
	if (config.enable_voting)
	{
		// Cache lock is held during callbacks, recursive calls are not allowed
		auto locked = rep_keys_cache.lock ();
		for (auto const & [pub, fan_ptr] : *locked)
		{
			lumex::raw_key prv;
			fan_ptr->value (prv);
			action (pub, prv);
		}
	}
}

void wallets::refresh_rep_keys_cache ()
{
	if (!config.enable_voting)
	{
		return;
	}

	std::vector<std::pair<lumex::public_key, std::unique_ptr<lumex::fan>>> new_cache;

	auto wallet_txn = tx_begin_read ();

	lumex::lock_guard<lumex::mutex> lock{ mutex };

	for (auto const & [id, wallet] : items)
	{
		lumex::lock_guard<std::recursive_mutex> store_lock{ wallet->store.mutex };

		auto reps_locked = wallet->representatives.lock ();
		for (auto const & account : *reps_locked)
		{
			if (wallet->store.exists (wallet_txn, account))
			{
				if (wallet->store.valid_password (wallet_txn))
				{
					auto prv_result = wallet->store.fetch (wallet_txn, account);
					release_assert (prv_result, "failed to fetch private key for representative account", account.to_account ());

					// Store private key spread across multiple heap allocations via fan to avoid plaintext keys in memory at rest
					new_cache.emplace_back (account, std::make_unique<lumex::fan> (prv_result.value (), config.password_fanout));
				}
				else
				{
					static auto last_log = std::chrono::steady_clock::time_point ();
					if (last_log < std::chrono::steady_clock::now () - std::chrono::seconds (60))
					{
						last_log = std::chrono::steady_clock::now ();
						logger.warn (lumex::log::type::wallet, "Representative locked inside wallet: {}", id);
					}
				}
			}
		}
	}
	rep_keys_cache.lock ()->swap (new_cache);
}

void wallets::run_reps_scan ()
{
	auto delay = [this] () {
		// Representation drifts quickly on the test network but very slowly on the live network
		return network_params.network.is_dev_network ()
		? 100ms
		: (network_params.network.is_test_network ()
		? std::chrono::milliseconds (lumex::test_scan_wallet_reps_delay ())
		: std::chrono::minutes (15));
	};

	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		lock.unlock ();

		stats.inc (lumex::stat::type::wallet, lumex::stat::detail::loop_reps);

		// Recompute local wallet representatives and refresh cached keys
		refresh_reps ();

		lock.lock ();

		reps_condition.wait_for (lock, delay (), [this] () {
			return stopped.load ();
		});
	}
}

void wallets::run_receivable_scan ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		lock.unlock ();

		stats.inc (lumex::stat::type::wallet, lumex::stat::detail::loop_receivable);

		// Reload wallets from disk
		reload ();

		// Search pending
		search_receivable_all ();

		lock.lock ();

		receivable_condition.wait_for (lock, network_params.node.search_pending_interval, [this] () {
			return stopped.load ();
		});
	}
}

void wallets::receive_confirmed (lumex::block_hash const & hash_a, lumex::account const & destination_a)
{
	auto wallets_l = all_wallets ();
	auto wallet_transaction = tx_begin_read ();
	for ([[maybe_unused]] auto const & [id, wallet] : wallets_l)
	{
		if (wallet->store.exists (wallet_transaction, destination_a))
		{
			lumex::account representative;
			representative = wallet->store.representative (wallet_transaction);
			auto pending = ledger.any.pending_get (ledger.tx_begin_read (), lumex::pending_key (destination_a, hash_a));
			if (pending)
			{
				auto amount (pending->amount.number ());
				wallet->receive_async (hash_a, representative, amount, destination_a, [] (std::shared_ptr<lumex::block> const &) {});
			}
			else
			{
				if (!ledger.cemented.block_exists_or_pruned (ledger.tx_begin_read (), hash_a))
				{
					logger.warn (lumex::log::type::wallet, "Confirmed block is missing: {}", hash_a);
					debug_assert (false, "confirmed block is missing");
				}
				else
				{
					logger.warn (lumex::log::type::wallet, "Block has already been received: {}", hash_a);
				}
			}
		}
	}
}

std::unordered_map<lumex::wallet_id, std::shared_ptr<wallet>> wallets::all_wallets ()
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	return items;
}

void wallets::do_wallet_actions ()
{
	lumex::unique_lock<lumex::mutex> action_lock{ action_mutex };
	while (!stopped)
	{
		if (!actions.empty ())
		{
			auto first (actions.begin ());
			auto wallet (first->second.first);
			auto current (std::move (first->second.second));
			actions.erase (first);
			// `wallets::destroy()` takes `action_mutex` before clearing `store.handle`,
			// so while we hold `action_lock` the handle is stable. The subsequent
			// `current(*wallet)` runs with `action_lock` released; anything inside the
			// action that touches `store.handle` must rely on its own synchronization
			// (e.g. LMDB's single-writer rule for `work_cache_blocking`).
			if (wallet->live ())
			{
				action_lock.unlock ();
				observer (true);
				current (*wallet);
				observer (false);
				action_lock.lock ();
			}
		}
		else
		{
			condition.wait (action_lock);
		}
	}
}

lumex::container_info wallets::container_info () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	lumex::container_info info;
	info.put ("items", items.size ());
	info.put ("actions", actions.size ());
	info.put ("rep_keys_cache", rep_keys_cache.lock ()->size ());
	return info;
}
}
