#pragma once

namespace lumex::store
{
enum class table;

class backend;
class meta_view;
class txn_tracking_config;
class ledger_store;
class read_transaction;
class transaction;
class write_transaction;
}

namespace lumex::store::ledger
{
class account_view;
class block_view;
class confirmation_height_view;
class final_vote_view;
class online_weight_view;
class peer_view;
class pending_view;
class pruned_view;
class successor_view;
class rep_weight_view;
class topology_view;

using version_view = lumex::store::meta_view;
}
