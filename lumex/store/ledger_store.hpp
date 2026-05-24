#pragma once

#include <lumex/lib/fwd.hpp>
#include <lumex/store/backend.hpp>
#include <lumex/store/common.hpp>
#include <lumex/store/fwd.hpp>
#include <lumex/store/write_queue.hpp>

#include <filesystem>
#include <memory>

namespace lumex::store
{
struct ledger_store_params
{
	bool backup_before_upgrade{ false };
	bool defer_open{ false }; // If true, skip automatic open/upgrade in constructor (for testing)
};

class ledger_store
{
public:
	explicit ledger_store (std::unique_ptr<lumex::store::backend>, lumex::store::open_mode mode, lumex::stats &, lumex::logger &, ledger_store_params = {});
	~ledger_store ();

	lumex::store::write_transaction tx_begin_write ();
	lumex::store::read_transaction tx_begin_read () const;

	bool empty (lumex::store::transaction const &) const;
	void perform_upgrades (lumex::store::backend_meta);

	uint64_t count (lumex::store::transaction const &, lumex::store::table) const;

	uint64_t get_version () const;
	std::string get_vendor () const;
	std::filesystem::path get_database_path () const;
	lumex::store::open_mode get_mode () const;

public: // Upgrades
	void upgrade_v21_to_v22 ();
	void upgrade_v22_to_v23 ();
	void upgrade_v23_to_v24 ();
	void upgrade_v24_to_v25 ();
	void upgrade_v25_to_v26 ();

public:
	lumex::store::write_queue write_queue; // TODO: Shouldn't be public

public:
	lumex::stats & stats;
	lumex::logger & logger;

private:
	std::unique_ptr<lumex::store::backend> backend_impl;
	std::unique_ptr<lumex::store::ledger::successor_view> successor_impl;
	std::unique_ptr<lumex::store::ledger::block_view> block_impl;
	std::unique_ptr<lumex::store::ledger::account_view> account_impl;
	std::unique_ptr<lumex::store::ledger::pending_view> pending_impl;
	std::unique_ptr<lumex::store::ledger::rep_weight_view> rep_weight_impl;
	std::unique_ptr<lumex::store::ledger::online_weight_view> online_weight_impl;
	std::unique_ptr<lumex::store::ledger::pruned_view> pruned_impl;
	std::unique_ptr<lumex::store::ledger::peer_view> peer_impl;
	std::unique_ptr<lumex::store::ledger::confirmation_height_view> confirmation_height_impl;
	std::unique_ptr<lumex::store::ledger::final_vote_view> final_vote_impl;
	std::unique_ptr<lumex::store::ledger::topology_view> topology_impl;
	std::unique_ptr<lumex::store::ledger::version_view> version_impl;

public:
	lumex::store::backend & backend;
	lumex::store::ledger::successor_view & successor;
	lumex::store::ledger::block_view & block;
	lumex::store::ledger::account_view & account;
	lumex::store::ledger::pending_view & pending;
	lumex::store::ledger::rep_weight_view & rep_weight;
	lumex::store::ledger::online_weight_view & online_weight;
	lumex::store::ledger::pruned_view & pruned;
	lumex::store::ledger::peer_view & peer;
	lumex::store::ledger::confirmation_height_view & confirmation_height;
	lumex::store::ledger::final_vote_view & final_vote;
	lumex::store::ledger::topology_view & topology;
	lumex::store::ledger::version_view & version;

public:
	static lumex::store::backend_version_t constexpr version_minimum{ 21 };
	static lumex::store::backend_version_t constexpr version_current{ 26 };

public:
	static lumex::store::column_schema const schema_current;
	static lumex::store::column_schema const schema_v21;
	static lumex::store::column_schema const schema_v22;
	static lumex::store::column_schema const schema_v23;
	static lumex::store::column_schema const schema_v24;
	static lumex::store::column_schema const schema_v25;
};
};