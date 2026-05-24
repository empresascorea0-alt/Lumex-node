#include <lumex/lib/errors.hpp>
#include <lumex/lib/utility.hpp>

#include <boost/system/error_code.hpp>

std::string lumex::error_common_messages::message (int ev) const
{
	switch (static_cast<lumex::error_common> (ev))
	{
		case lumex::error_common::generic:
			return "Unknown error";
		case lumex::error_common::access_denied:
			return "Access denied";
		case lumex::error_common::missing_account:
			return "Missing account";
		case lumex::error_common::missing_balance:
			return "Missing balance";
		case lumex::error_common::missing_link:
			return "Missing link, source or destination";
		case lumex::error_common::missing_previous:
			return "Missing previous";
		case lumex::error_common::missing_representative:
			return "Missing representative";
		case lumex::error_common::missing_signature:
			return "Missing signature";
		case lumex::error_common::missing_work:
			return "Missing work";
		case lumex::error_common::exception:
			return "Exception thrown";
		case lumex::error_common::account_exists:
			return "Account already exists";
		case lumex::error_common::account_not_found:
			return "Account not found";
		case lumex::error_common::account_not_found_wallet:
			return "Account not found in wallet";
		case lumex::error_common::bad_account_number:
			return "Bad account number";
		case lumex::error_common::bad_balance:
			return "Bad balance";
		case lumex::error_common::bad_link:
			return "Bad link value";
		case lumex::error_common::bad_previous:
			return "Bad previous hash";
		case lumex::error_common::bad_representative_number:
			return "Bad representative";
		case lumex::error_common::bad_source:
			return "Bad source";
		case lumex::error_common::bad_signature:
			return "Bad signature";
		case lumex::error_common::bad_private_key:
			return "Bad private key";
		case lumex::error_common::bad_public_key:
			return "Bad public key";
		case lumex::error_common::bad_seed:
			return "Bad seed";
		case lumex::error_common::bad_threshold:
			return "Bad threshold number";
		case lumex::error_common::bad_wallet_number:
			return "Bad wallet number";
		case lumex::error_common::bad_work_format:
			return "Bad work";
		case lumex::error_common::disabled_local_work_generation:
			return "Local work generation is disabled";
		case lumex::error_common::disabled_work_generation:
			return "Work generation is disabled";
		case lumex::error_common::failure_work_generation:
			return "Work generation cancellation or failure";
		case lumex::error_common::insufficient_balance:
			return "Insufficient balance";
		case lumex::error_common::invalid_amount:
			return "Invalid amount number";
		case lumex::error_common::invalid_amount_big:
			return "Amount too big";
		case lumex::error_common::invalid_count:
			return "Invalid count";
		case lumex::error_common::invalid_ip_address:
			return "Invalid IP address";
		case lumex::error_common::invalid_port:
			return "Invalid port";
		case lumex::error_common::invalid_index:
			return "Invalid index";
		case lumex::error_common::invalid_type_conversion:
			return "Invalid type conversion";
		case lumex::error_common::invalid_work:
			return "Invalid work";
		case lumex::error_common::is_not_state_block:
			return "Must be a state block";
		case lumex::error_common::numeric_conversion:
			return "Numeric conversion error";
		case lumex::error_common::tracking_not_enabled:
			return "Database transaction tracking is not enabled in the config";
		case lumex::error_common::wallet_lmdb_max_dbs:
			return "Failed to create wallet. Increase lmdb_max_dbs in node config";
		case lumex::error_common::wallet_locked:
			return "Wallet is locked";
		case lumex::error_common::wallet_not_found:
			return "Wallet not found";
	}

	return "Invalid error code";
}

std::string lumex::error_blocks_messages::message (int ev) const
{
	switch (static_cast<lumex::error_blocks> (ev))
	{
		case lumex::error_blocks::generic:
			return "Unknown error";
		case lumex::error_blocks::bad_hash_number:
			return "Bad hash number";
		case lumex::error_blocks::invalid_block:
			return "Block is invalid";
		case lumex::error_blocks::invalid_block_hash:
			return "Invalid block hash";
		case lumex::error_blocks::invalid_type:
			return "Invalid block type";
		case lumex::error_blocks::not_found:
			return "Block not found";
		case lumex::error_blocks::work_low:
			return "Block work is less than threshold";
	}

	return "Invalid error code";
}

std::string lumex::error_rpc_messages::message (int ev) const
{
	switch (static_cast<lumex::error_rpc> (ev))
	{
		case lumex::error_rpc::generic:
			return "Unknown error";
		case lumex::error_rpc::empty_response:
			return "Empty response";
		case lumex::error_rpc::bad_destination:
			return "Bad destination account";
		case lumex::error_rpc::bad_difficulty_format:
			return "Bad difficulty";
		case lumex::error_rpc::bad_key:
			return "Bad key";
		case lumex::error_rpc::bad_link:
			return "Bad link number";
		case lumex::error_rpc::bad_multiplier_format:
			return "Bad multiplier";
		case lumex::error_rpc::bad_previous:
			return "Bad previous";
		case lumex::error_rpc::bad_representative_number:
			return "Bad representative number";
		case lumex::error_rpc::bad_source:
			return "Bad source";
		case lumex::error_rpc::bad_timeout:
			return "Bad timeout number";
		case lumex::error_rpc::bad_work_version:
			return "Bad work version";
		case lumex::error_rpc::block_create_balance_mismatch:
			return "Balance mismatch for previous block";
		case lumex::error_rpc::block_create_key_required:
			return "Private key or local wallet and account required";
		case lumex::error_rpc::block_create_public_key_mismatch:
			return "Incorrect key for given account";
		case lumex::error_rpc::block_create_requirements_state:
			return "Previous, representative, final balance and link (source or destination) are required";
		case lumex::error_rpc::block_create_requirements_open:
			return "Representative account and source hash required";
		case lumex::error_rpc::block_create_requirements_receive:
			return "Previous hash and source hash required";
		case lumex::error_rpc::block_create_requirements_change:
			return "Representative account and previous hash required";
		case lumex::error_rpc::block_create_requirements_send:
			return "Destination account, previous hash, current balance and amount required";
		case lumex::error_rpc::block_root_mismatch:
			return "Root mismatch for block";
		case lumex::error_rpc::block_work_enough:
			return "Provided work is already enough for given difficulty";
		case lumex::error_rpc::block_work_version_mismatch:
			return "Work version mismatch for block";
		case lumex::error_rpc::confirmation_height_not_processing:
			return "There are no blocks currently being processed for adding confirmation height";
		case lumex::error_rpc::confirmation_not_found:
			return "Active confirmation not found";
		case lumex::error_rpc::difficulty_limit:
			return "Difficulty above config limit or below publish threshold";
		case lumex::error_rpc::disabled_bootstrap_lazy:
			return "Lazy bootstrap is disabled";
		case lumex::error_rpc::disabled_bootstrap_legacy:
			return "Legacy bootstrap is disabled";
		case lumex::error_rpc::invalid_balance:
			return "Invalid balance number";
		case lumex::error_rpc::invalid_destinations:
			return "Invalid destinations number";
		case lumex::error_rpc::invalid_epoch:
			return "Invalid epoch number";
		case lumex::error_rpc::invalid_epoch_signer:
			return "Incorrect epoch signer";
		case lumex::error_rpc::invalid_offset:
			return "Invalid offset";
		case lumex::error_rpc::invalid_missing_type:
			return "Invalid or missing type argument";
		case lumex::error_rpc::invalid_root:
			return "Invalid root hash";
		case lumex::error_rpc::invalid_sources:
			return "Invalid sources number";
		case lumex::error_rpc::invalid_subtype:
			return "Invalid block subtype";
		case lumex::error_rpc::invalid_subtype_balance:
			return "Invalid block balance for given subtype";
		case lumex::error_rpc::invalid_subtype_epoch_link:
			return "Invalid epoch link";
		case lumex::error_rpc::invalid_subtype_previous:
			return "Invalid previous block for given subtype";
		case lumex::error_rpc::invalid_timestamp:
			return "Invalid timestamp";
		case lumex::error_rpc::invalid_threads_count:
			return "Invalid threads count";
		case lumex::error_rpc::peer_not_found:
			return "Peer not found";
		case lumex::error_rpc::pruning_disabled:
			return "Pruning is disabled";
		case lumex::error_rpc::requires_port_and_address:
			return "Both port and address required";
		case lumex::error_rpc::rpc_control_disabled:
			return "RPC control is disabled";
		case lumex::error_rpc::sign_hash_disabled:
			return "Signing by block hash is disabled";
		case lumex::error_rpc::source_not_found:
			return "Source not found";
		case lumex::error_rpc::stopped:
			return "Stopped";
	}

	return "Invalid error code";
}

std::string lumex::error_process_messages::message (int ev) const
{
	switch (static_cast<lumex::error_process> (ev))
	{
		case lumex::error_process::generic:
			return "Unknown error";
		case lumex::error_process::bad_signature:
			return "Bad signature";
		case lumex::error_process::old:
			return "Old block";
		case lumex::error_process::negative_spend:
			return "Negative spend";
		case lumex::error_process::fork:
			return "Fork";
		case lumex::error_process::unreceivable:
			return "Unreceivable";
		case lumex::error_process::gap_previous:
			return "Gap previous block";
		case lumex::error_process::gap_source:
			return "Gap source block";
		case lumex::error_process::gap_epoch_open_pending:
			return "Gap pending for open epoch block";
		case lumex::error_process::opened_burn_account:
			return "Block attempts to open the burn account";
		case lumex::error_process::balance_mismatch:
			return "Balance and amount delta do not match";
		case lumex::error_process::block_position:
			return "This block cannot follow the previous block";
		case lumex::error_process::insufficient_work:
			return "Block work is insufficient";
		case lumex::error_process::other:
			return "Error processing block";
	}

	return "Invalid error code";
}

std::string lumex::error_config_messages::message (int ev) const
{
	switch (static_cast<lumex::error_config> (ev))
	{
		case lumex::error_config::generic:
			return "Unknown error";
		case lumex::error_config::invalid_value:
			return "Invalid configuration value";
		case lumex::error_config::missing_value:
			return "Missing value in configuration";
	}

	return "Invalid error code";
}

/*
 *
 */

lumex::error::error (std::error_code code_a)
{
	code = code_a;
}

lumex::error::error (std::string message_a)
{
	code = lumex::error_common::generic;
	message = std::move (message_a);
}

lumex::error::error (std::exception const & exception_a)
{
	code = lumex::error_common::exception;
	message = exception_a.what ();
}

lumex::error & lumex::error::operator= (lumex::error const & err_a)
{
	code = err_a.code;
	message = err_a.message;
	return *this;
}

lumex::error & lumex::error::operator= (lumex::error && err_a)
{
	code = err_a.code;
	message = std::move (err_a.message);
	return *this;
}

/** Assign error code */
lumex::error & lumex::error::operator= (std::error_code const code_a)
{
	code = code_a;
	message.clear ();
	return *this;
}

/** Set the error to lumex::error_common::generic and the error message to \p message_a */
lumex::error & lumex::error::operator= (std::string message_a)
{
	code = lumex::error_common::generic;
	message = std::move (message_a);
	return *this;
}

/** Sets the error to lumex::error_common::exception and adopts the exception error message. */
lumex::error & lumex::error::operator= (std::exception const & exception_a)
{
	code = lumex::error_common::exception;
	message = exception_a.what ();
	return *this;
}

/** Return true if this#error_code equals the parameter */
bool lumex::error::operator== (std::error_code const code_a) const
{
	return code == code_a;
}

/** Call the function iff the current error is zero */
lumex::error & lumex::error::then (std::function<lumex::error &()> next)
{
	return code ? *this : next ();
}

/** Implicit error_code conversion */
lumex::error::operator std::error_code () const
{
	return code;
}

int lumex::error::error_code_as_int () const
{
	return code.value ();
}

/** Implicit bool conversion; true if there's an error */
lumex::error::operator bool () const
{
	return code.value () != 0;
}

/** Implicit string conversion; returns the error message or an empty string. */
lumex::error::operator std::string () const
{
	return get_message ();
}

/**
 * Get error message, or an empty string if there's no error. If a custom error message is set,
 * that will be returned, otherwise the error_code#message() is returned.
 */
std::string lumex::error::get_message () const
{
	std::string res = message;
	if (code && res.empty ())
	{
		res = code.message ();
	}
	return res;
}

/** Set an error message, but only if the error code is already set */
lumex::error & lumex::error::on_error (std::string message_a)
{
	if (code)
	{
		message = std::move (message_a);
	}
	return *this;
}

/** Set an error message if the current error code matches \p code_a */
lumex::error & lumex::error::on_error (std::error_code code_a, std::string message_a)
{
	if (code == code_a)
	{
		message = std::move (message_a);
	}
	return *this;
}

/** Set an error message and an error code */
lumex::error & lumex::error::set (std::string message_a, std::error_code code_a)
{
	message = std::move (message_a);
	code = code_a;
	return *this;
}

/** Set a custom error message. If the error code is not set, it will be set to lumex::error_common::generic. */
lumex::error & lumex::error::set_message (std::string message_a)
{
	if (!code)
	{
		code = lumex::error_common::generic;
	}
	message = std::move (message_a);
	return *this;
}

/** Clear an errors */
lumex::error & lumex::error::clear ()
{
	code.clear ();
	message.clear ();
	return *this;
}
