#pragma once

#include <lumex/lib/fwd.hpp>
#include <lumex/store/fwd.hpp>

namespace lumex
{
class account_info;
class ledger;
class ledger_cache;
class ledger_constants;
class network_params;
class pending_info;
class pending_key;
class vote_permit;
class voting_policy;

enum class block_status;
enum class vote_type;
}

namespace lumex::secure
{
class read_transaction;
class transaction;
class write_transaction;
}
