#pragma once

#include <lumex/lib/fan.hpp>
#include <lumex/lib/kdf.hpp>
#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/lib/result.hpp>
#include <lumex/lib/thread_pool.hpp>
#include <lumex/lib/work.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/node/openclwork.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/store/typed_iterator.hpp>
#include <lumex/wallet/wallet_value.hpp>
#include <lumex/wallet/wallets_backend.hpp>

#include <mutex>
#include <optional>
#include <thread>
#include <unordered_set>

namespace lumex::wallet
{
enum class key_type
{
	not_a_type,
	unknown,
	adhoc,
	deterministic
};

class wallet_store;

/**
 * Validated handle for wallet-key crypto. The only way to obtain one is via wallet_store::unlock(), which has therefore already validated the password.
 * The cipher exposes encrypt/decrypt but never the underlying wallet key, so it is impossible to encrypt or decrypt with an unvalidated key.
 */
class wallet_cipher final
{
public:
	lumex::raw_key encrypt (lumex::raw_key const & plaintext, lumex::uint128_union const & iv) const;
	lumex::raw_key decrypt (lumex::uint256_union const & ciphertext, lumex::uint128_union const & iv) const;

	// Re-encrypt the wallet key under a new password key. Used by rekey.
	lumex::raw_key reseal (lumex::raw_key const & new_password_key, lumex::uint128_union const & iv) const;

private:
	friend class wallet_store;
	explicit wallet_cipher (lumex::raw_key wallet_key);

	lumex::raw_key wallet_key;
};

class wallet_store final
{
public:
	using iterator = store::typed_iterator<lumex::account, lumex::wallet::wallet_value>;

public:
	wallet_store (lumex::kdf &, lumex::store::write_transaction &, lumex::wallet::wallets_backend &, lumex::account representative, unsigned fanout, std::string const & wallet_path);
	wallet_store (lumex::kdf &, lumex::store::write_transaction &, lumex::wallet::wallets_backend &, unsigned fanout, std::string const & wallet_path, std::string const & json);

	// Returns a cipher if the password decrypts the wallet key correctly.
	// The cipher is the only way to encrypt/decrypt account data.
	std::optional<lumex::wallet::wallet_cipher> unlock (lumex::store::transaction const &) const;

	std::vector<lumex::account> accounts (lumex::store::transaction const &) const;
	bool rekey (lumex::store::write_transaction const &, std::string const & password);
	bool valid_password (lumex::store::transaction const &) const;
	bool valid_public_key (lumex::public_key const &) const;
	bool attempt_password (lumex::store::transaction const &, std::string const & password);
	lumex::raw_key seed (lumex::store::transaction const &) const;
	void seed_set (lumex::store::write_transaction const &, lumex::raw_key const & seed);
	lumex::wallet::key_type key_type (lumex::wallet::wallet_value const &) const;
	lumex::public_key deterministic_insert (lumex::store::write_transaction const &);
	lumex::public_key deterministic_insert (lumex::store::write_transaction const &, uint32_t index);
	lumex::raw_key deterministic_key (lumex::store::transaction const &, uint32_t index) const;
	uint32_t deterministic_index_get (lumex::store::transaction const &) const;
	void deterministic_index_set (lumex::store::write_transaction const &, uint32_t index);
	void deterministic_clear (lumex::store::write_transaction const &);
	lumex::uint256_union salt_get (lumex::store::transaction const &) const;
	lumex::uint256_union check_value_get (lumex::store::transaction const &) const;
	bool is_representative (lumex::store::transaction const &) const;
	lumex::account representative (lumex::store::transaction const &) const;
	void representative_set (lumex::store::write_transaction const &, lumex::account const & rep);
	lumex::public_key insert_adhoc (lumex::store::write_transaction const &, lumex::raw_key const & prv);
	bool insert_watch (lumex::store::write_transaction const &, lumex::account const & pub);
	void erase (lumex::store::write_transaction const &, lumex::account const & pub);
	lumex::wallet::wallet_value entry_get_raw (lumex::store::transaction const &, lumex::account const & pub) const;
	void entry_put_raw (lumex::store::write_transaction const &, lumex::account const & pub, lumex::wallet::wallet_value const & entry);
	lumex::result<lumex::raw_key> fetch (lumex::store::transaction const &, lumex::account const & pub) const;
	bool exists (lumex::store::transaction const &, lumex::account const & pub) const;
	void destroy (lumex::store::write_transaction const &);
	iterator find (lumex::store::transaction const &, lumex::account const & key) const;
	iterator begin (lumex::store::transaction const &, lumex::account const & key) const;
	iterator begin (lumex::store::transaction const &) const;
	iterator end (lumex::store::transaction const &) const;
	lumex::raw_key derive_key (lumex::store::transaction const &, std::string const & password) const;
	void serialize_json (lumex::store::transaction const &, std::string & json) const;
	void write_backup (lumex::store::transaction const &, std::filesystem::path const & path) const;
	lumex::result<bool> move (lumex::store::write_transaction const &, wallet_store & source, std::vector<lumex::public_key> const & keys);
	lumex::result<bool> import (lumex::store::write_transaction const &, wallet_store & source);
	std::optional<uint64_t> work_get (lumex::store::transaction const &, lumex::public_key const &) const;
	void work_put (lumex::store::write_transaction const &, lumex::public_key const & pub, uint64_t work);
	unsigned version (lumex::store::transaction const &) const;
	void version_put (lumex::store::write_transaction const &, unsigned version);

public:
	lumex::fan password;
	lumex::fan wallet_key_mem;
	lumex::kdf & kdf;
	lumex::locked<lumex::wallet::wallet_handle> handle;
	mutable std::recursive_mutex mutex;

private:
	// Decrypts the wallet key using whatever the live password fan currently is.
	// Used only by unlock() and during construction; callers outside wallet_store
	// must go through unlock() so that the password is validated first.
	lumex::raw_key wallet_key_decrypt (lumex::store::transaction const &) const;

	// Decrypts the wallet seed using an already-unlocked cipher.
	// Centralises the seed_special / seed_iv_index plumbing so callers can't drift.
	lumex::raw_key seed_decrypt (lumex::store::transaction const &, lumex::wallet::wallet_cipher const &) const;

private:
	lumex::wallet::wallets_backend & backend;

public:
	static unsigned const version_1 = 1;
	static unsigned const version_2 = 2;
	static unsigned const version_3 = 3;
	static unsigned const version_4 = 4;
	static unsigned constexpr version_current = version_4;
	static lumex::account const version_special;
	static lumex::account const wallet_key_special;
	static lumex::account const salt_special;
	static lumex::account const check_special;
	static lumex::account const representative_special;
	static lumex::account const seed_special;
	static lumex::account const deterministic_index_special;
	static std::size_t const check_iv_index;
	static std::size_t const seed_iv_index;
	static int const special_count;
	static std::string const default_password;
};

/**
 * A wallet is a set of account keys encrypted by a common encryption key
 */
class wallet final : public std::enable_shared_from_this<wallet>
{
public:
	wallet (lumex::store::write_transaction &, wallets &, std::string const & wallet_path);
	wallet (lumex::store::write_transaction &, wallets &, std::string const & wallet_path, std::string const & json);

	// Password and lock management
	void enter_initial_password ();
	bool enter_password (std::string const & password);
	bool rekey (std::string const & password);
	bool is_locked () const;
	void lock ();

	// Account management
	lumex::result<lumex::public_key> insert_adhoc (lumex::raw_key const & prv, bool generate_work = true);
	lumex::result<lumex::public_key> deterministic_insert (uint32_t index, bool generate_work = true);
	lumex::result<lumex::public_key> deterministic_insert (bool generate_work = true);
	bool insert_watch (lumex::public_key const & pub);
	void remove_account (lumex::account const & account);
	std::vector<lumex::account> accounts () const;
	bool exists (lumex::public_key const & pub);
	lumex::result<bool> move_accounts (wallet & source, std::vector<lumex::public_key> const & accounts);
	lumex::wallet::key_type key_type (lumex::account const & account) const;

	// Seed management
	lumex::result<lumex::raw_key> get_seed () const;
	lumex::public_key change_seed (lumex::raw_key const & seed, uint32_t count = 0);
	void deterministic_restore ();
	uint32_t deterministic_check (uint32_t index);
	uint32_t get_deterministic_index () const;

	// Representative management
	void set_representative (lumex::account const & rep);
	lumex::account get_representative () const;

	// Local wallet representatives
	std::unordered_set<lumex::account> reps () const;

	// Key retrieval
	lumex::result<lumex::raw_key> fetch_prv (lumex::account const & pub) const;

	// Block actions
	std::shared_ptr<lumex::block> change_action (lumex::account const & source, lumex::account const & representative, uint64_t work = 0, bool generate_work = true);
	std::shared_ptr<lumex::block> receive_action (lumex::block_hash const & send_hash, lumex::account const & representative, lumex::uint128_union const & amount, lumex::account const & account, uint64_t work = 0, bool generate_work = true);
	std::shared_ptr<lumex::block> send_action (lumex::account const & source, lumex::account const & destination, lumex::uint128_t const & amount, uint64_t work = 0, bool generate_work = true, std::optional<std::string> id = {});
	bool action_complete (std::shared_ptr<lumex::block> const & block, lumex::account const & account, bool generate_work, lumex::block_details const & details);

	// Sync/async block operations
	bool change_sync (lumex::account const & source, lumex::account const & representative);
	void change_async (lumex::account const & source, lumex::account const & representative, std::function<void (std::shared_ptr<lumex::block> const &)> const & action, uint64_t work = 0, bool generate_work = true);
	bool receive_sync (std::shared_ptr<lumex::block> const & block, lumex::account const & representative, lumex::uint128_t const & amount);
	void receive_async (lumex::block_hash const & hash, lumex::account const & representative, lumex::uint128_t const & amount, lumex::account const & account, std::function<void (std::shared_ptr<lumex::block> const &)> const & action, uint64_t work = 0, bool generate_work = true);
	lumex::block_hash send_sync (lumex::account const & source, lumex::account const & destination, lumex::uint128_t const & amount);
	void send_async (lumex::account const & source, lumex::account const & destination, lumex::uint128_t const & amount, std::function<void (std::shared_ptr<lumex::block> const &)> const & action, uint64_t work = 0, bool generate_work = true, std::optional<std::string> id = {});

	// Work cache
	void work_cache_blocking (lumex::account const & account, lumex::root const & root);
	void work_ensure (lumex::account const & account, lumex::root const & root);
	lumex::result<uint64_t> get_work (lumex::public_key const &) const;
	void set_work (lumex::public_key const & pub, uint64_t work);

	// Receivable
	bool search_receivable ();

	// Import/export
	bool import (std::string const & json, std::string const & password);
	void serialize_json (std::string & json);
	void write_backup (std::filesystem::path const & path);

	// Status
	bool live ();

public:
	std::unordered_set<lumex::account> free_accounts;
	std::function<void (bool, bool)> lock_observer;
	lumex::wallet::wallet_store store;
	lumex::wallet::wallets & wallets;
	lumex::logger & logger;

private:
	// Internal implementation methods (accept transactions for batching scenarios)
	bool enter_password_impl (lumex::store::transaction const &, std::string const & password);
	bool insert_watch_impl (lumex::store::write_transaction const &, lumex::public_key const & pub);
	lumex::public_key deterministic_insert_impl (lumex::store::write_transaction const &, bool generate_work = true);
	lumex::public_key deterministic_insert_impl (lumex::store::write_transaction const &, uint32_t index, bool generate_work = true);
	void work_update_impl (lumex::store::write_transaction const &, lumex::account const & account, lumex::root const & root, uint64_t work);
	bool search_receivable_impl (lumex::store::transaction const &);
	void init_free_accounts_impl (lumex::store::transaction const &);
	uint32_t deterministic_check_impl (lumex::store::transaction const &, uint32_t index);
	lumex::public_key change_seed_impl (lumex::store::write_transaction const &, lumex::raw_key const & seed, uint32_t count = 0);
	void deterministic_restore_impl (lumex::store::write_transaction const &);

private:
	lumex::locked<std::unordered_set<lumex::account>> representatives;

	friend class wallets;
};

class wallet_representatives
{
public:
	uint64_t voting{ 0 }; // Number of representatives with at least the configured minimum voting weight
	bool half_principal{ false }; // has representatives with at least 50% of principal representative requirements
	std::unordered_set<lumex::account> accounts; // Representatives with at least the configured minimum voting weight
	bool have_half_rep () const
	{
		return half_principal;
	}
	bool exists (lumex::account const & rep_a) const
	{
		return accounts.count (rep_a) > 0;
	}
	void clear ()
	{
		voting = 0;
		half_principal = false;
		accounts.clear ();
	}
};

/**
 * The wallets set is all the wallets a node controls.
 * A node may contain multiple wallets independently encrypted and operated.
 */
class wallets final
{
public:
	wallets (
	lumex::node &,
	lumex::wallet::wallets_backend &,
	lumex::ledger &,
	lumex::node_config const &,
	lumex::network_params const &,
	lumex::online_reps &,
	lumex::network &,
	lumex::stats &,
	lumex::logger &);

	~wallets ();

	void start ();
	void stop ();

	// Wallet management
	std::shared_ptr<wallet> open (lumex::wallet_id const &);
	std::shared_ptr<wallet> create (lumex::wallet_id const &);
	std::shared_ptr<wallet> create_from_json (lumex::wallet_id const &, std::string const & json);
	void destroy (lumex::wallet_id const &);
	void reload ();
	void clear_send_ids ();

	// Account lookup
	std::unordered_map<lumex::wallet_id, std::shared_ptr<wallet>> all_wallets ();
	bool exists (lumex::account const &);
	bool exists_any (lumex::account const &, lumex::account const &);

	// Receivable
	bool search_receivable (lumex::wallet_id const &);
	void search_receivable_all ();
	void receive_confirmed (lumex::block_hash const & hash, lumex::account const & destination);

	// Wallet actions queue
	void do_wallet_actions ();
	void queue_wallet_action (lumex::uint128_t const & priority, std::shared_ptr<wallet> const &, std::function<void (wallet &)> action);

	// Representatives
	void foreach_representative (std::function<void (lumex::public_key const &, lumex::raw_key const &)> const & action);
	bool check_rep (lumex::account const &);
	void refresh_reps ();
	wallet_representatives reps () const;

	/// Returns a signer that iterates over all representatives in the wallet
	using signer_t = std::function<void (std::function<void (lumex::public_key const &, lumex::raw_key const &)> const &)>;
	signer_t signer ();

	lumex::container_info container_info () const;

private: // Transactions
	lumex::store::write_transaction tx_begin_write ();
	lumex::store::read_transaction tx_begin_read ();

public: // Dependencies
	lumex::node & node;
	lumex::wallet::wallets_backend & backend;
	lumex::ledger & ledger;
	lumex::node_config const & config;
	lumex::network_params const & network_params;
	lumex::online_reps & online_reps;
	lumex::network & network;
	lumex::stats & stats;
	lumex::logger & logger;

public:
	std::function<void (bool)> observer;

	std::unordered_map<lumex::wallet_id, std::shared_ptr<wallet>> items;
	std::multimap<lumex::uint128_t, std::pair<std::shared_ptr<wallet>, std::function<void (wallet &)>>, std::greater<lumex::uint128_t>> actions;
	lumex::locked<std::unordered_map<lumex::account, lumex::root>> delayed_work;

	lumex::kdf kdf;

	mutable lumex::mutex mutex;
	mutable lumex::mutex action_mutex;
	lumex::condition_variable condition;
	lumex::condition_variable reps_condition;
	lumex::condition_variable receivable_condition;
	std::atomic<bool> stopped{ false };
	std::thread thread;
	std::thread reps_thread;
	std::thread receivable_thread;

	lumex::thread_pool workers;

	static lumex::uint128_t const generate_priority;
	static lumex::uint128_t const high_priority;

private:
	void run_reps_scan ();
	void run_receivable_scan ();
	bool check_rep_impl (wallet_representatives &, lumex::account const &, lumex::uint128_t const & half_principal_weight);
	bool exists_impl (lumex::store::transaction const &, lumex::account const &);
	void refresh_rep_index ();
	void refresh_rep_keys_cache ();

private:
	mutable lumex::locked<wallet_representatives> representatives;
	lumex::locked<std::vector<std::pair<lumex::public_key, std::unique_ptr<lumex::fan>>>> rep_keys_cache;

	friend class wallet;
};
}
