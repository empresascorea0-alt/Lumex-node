#pragma once

#include <lumex/lib/lmdbconfig.hpp>
#include <lumex/store/lmdb/lmdb_env.hpp>
#include <lumex/wallet/wallets_backend.hpp>

#include <string_view>

namespace lumex::wallet::lmdb
{
class wallets_backend_lmdb final : public lumex::wallet::wallets_backend
{
public:
	explicit wallets_backend_lmdb (std::filesystem::path const &, lumex::lmdb_config const & = lumex::lmdb_config{});

	lumex::store::read_transaction tx_begin_read () const override;
	lumex::store::write_transaction tx_begin_write () override;

	wallet_handle wallet_open_or_create (lumex::store::write_transaction const &, std::string const & wallet_id_hex) override;
	void wallet_drop (lumex::store::write_transaction const &, wallet_handle &) override;

	std::optional<lumex::store::db_val> entry_get (lumex::store::transaction const &, wallet_handle const &, lumex::store::db_val const & key) const override;
	void entry_put (lumex::store::write_transaction const &, wallet_handle const &, lumex::store::db_val const & key, lumex::store::db_val const & value) override;
	void entry_del (lumex::store::write_transaction const &, wallet_handle const &, lumex::store::db_val const & key) override;
	bool entry_exists (lumex::store::transaction const &, wallet_handle const &, lumex::store::db_val const & key) const override;

	lumex::store::iterator entries_begin (lumex::store::transaction const &, wallet_handle const &) const override;
	lumex::store::iterator entries_begin (lumex::store::transaction const &, wallet_handle const &, lumex::store::db_val const & key) const override;
	lumex::store::iterator entries_end (lumex::store::transaction const &, wallet_handle const &) const override;

	lumex::store::iterator index_begin (lumex::store::transaction const &) const override;
	lumex::store::iterator index_begin (lumex::store::transaction const &, lumex::store::db_val const & key) const override;
	lumex::store::iterator index_end (lumex::store::transaction const &) const override;

	std::optional<lumex::store::db_val> send_action_id_get (lumex::store::transaction const &, lumex::store::db_val const & id) const override;
	bool send_action_id_put (lumex::store::write_transaction const &, lumex::store::db_val const & id, lumex::store::db_val const & value) override;
	void send_action_ids_clear (lumex::store::write_transaction const &) override;

	std::filesystem::path database_path () const override;
	void backup (lumex::logger &) const override;

private:
	static constexpr std::string_view send_action_ids_name{ "send_action_ids" };

	static MDB_dbi handle_for (wallet_handle const &);

	lumex::store::lmdb::env environment;

	// LMDB's default DB, which doubles as the catalog of every named sub-DB in this env.
	// Entries appear/disappear implicitly via `mdb_dbi_open(MDB_CREATE)` / `mdb_drop(..., 1)`; we never write to it directly.
	MDB_dbi wallet_index_dbi{};
	MDB_dbi send_action_ids_dbi{};
};
}
