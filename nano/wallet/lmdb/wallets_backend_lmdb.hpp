#pragma once

#include <nano/lib/lmdbconfig.hpp>
#include <nano/store/lmdb/lmdb_env.hpp>
#include <nano/wallet/wallets_backend.hpp>

#include <string_view>

namespace nano::wallet::lmdb
{
class wallets_backend_lmdb final : public nano::wallet::wallets_backend
{
public:
	explicit wallets_backend_lmdb (std::filesystem::path const &, nano::lmdb_config const & = nano::lmdb_config{});

	nano::store::read_transaction tx_begin_read () const override;
	nano::store::write_transaction tx_begin_write () override;

	wallet_handle wallet_open_or_create (nano::store::write_transaction const &, std::string const & wallet_id_hex) override;
	void wallet_drop (nano::store::write_transaction const &, wallet_handle &) override;

	std::optional<nano::store::db_val> entry_get (nano::store::transaction const &, wallet_handle const &, nano::store::db_val const & key) const override;
	void entry_put (nano::store::write_transaction const &, wallet_handle const &, nano::store::db_val const & key, nano::store::db_val const & value) override;
	void entry_del (nano::store::write_transaction const &, wallet_handle const &, nano::store::db_val const & key) override;
	bool entry_exists (nano::store::transaction const &, wallet_handle const &, nano::store::db_val const & key) const override;

	nano::store::iterator entries_begin (nano::store::transaction const &, wallet_handle const &) const override;
	nano::store::iterator entries_begin (nano::store::transaction const &, wallet_handle const &, nano::store::db_val const & key) const override;
	nano::store::iterator entries_end (nano::store::transaction const &, wallet_handle const &) const override;

	nano::store::iterator index_begin (nano::store::transaction const &) const override;
	nano::store::iterator index_begin (nano::store::transaction const &, nano::store::db_val const & key) const override;
	nano::store::iterator index_end (nano::store::transaction const &) const override;

	std::optional<nano::store::db_val> send_action_id_get (nano::store::transaction const &, nano::store::db_val const & id) const override;
	bool send_action_id_put (nano::store::write_transaction const &, nano::store::db_val const & id, nano::store::db_val const & value) override;
	void send_action_ids_clear (nano::store::write_transaction const &) override;

	std::filesystem::path database_path () const override;
	void backup (nano::logger &) const override;

private:
	static constexpr std::string_view send_action_ids_name{ "send_action_ids" };

	static MDB_dbi handle_for (wallet_handle const &);

	nano::store::lmdb::env environment;

	// LMDB's default DB, which doubles as the catalog of every named sub-DB in this env.
	// Entries appear/disappear implicitly via `mdb_dbi_open(MDB_CREATE)` / `mdb_drop(..., 1)`; we never write to it directly.
	MDB_dbi wallet_index_dbi{};
	MDB_dbi send_action_ids_dbi{};
};
}
