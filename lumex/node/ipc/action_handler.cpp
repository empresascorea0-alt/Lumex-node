#include <lumex/ipc_flatbuffers_lib/generated/flatbuffers/lumexapi_generated.h>
#include <lumex/lib/errors.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/node/ipc/action_handler.hpp>
#include <lumex/node/ipc/ipc_server.hpp>
#include <lumex/node/node.hpp>

#include <boost/multiprecision/cpp_int.hpp>

namespace
{
lumex::account parse_account (std::string const & account, bool & out_is_deprecated_format)
{
	lumex::account result{};
	if (account.empty ())
	{
		throw lumex::error (lumex::error_common::bad_account_number);
	}

	if (result.decode_account (account))
	{
		throw lumex::error (lumex::error_common::bad_account_number);
	}
	else if (account[3] == '-' || account[4] == '-')
	{
		out_is_deprecated_format = true;
	}

	return result;
}
/** Returns the message as a Flatbuffers ObjectAPI type, managed by a unique_ptr */
template <typename T>
auto get_message (lumexapi::Envelope const & envelope)
{
	auto raw (envelope.message_as<T> ()->UnPack ());
	return std::unique_ptr<typename T::NativeTableType> (raw);
}
}

/**
 * Mapping from message type to handler function.
 * @note This must be updated whenever a new message type is added to the Flatbuffers IDL.
 */
auto lumex::ipc::action_handler::handler_map () -> std::unordered_map<lumexapi::Message, std::function<void (lumex::ipc::action_handler *, lumexapi::Envelope const &)>, lumex::ipc::enum_hash>
{
	static std::unordered_map<lumexapi::Message, std::function<void (lumex::ipc::action_handler *, lumexapi::Envelope const &)>, lumex::ipc::enum_hash> handlers;
	if (handlers.empty ())
	{
		handlers.emplace (lumexapi::Message::Message_IsAlive, &lumex::ipc::action_handler::on_is_alive);
		handlers.emplace (lumexapi::Message::Message_TopicConfirmation, &lumex::ipc::action_handler::on_topic_confirmation);
		handlers.emplace (lumexapi::Message::Message_AccountWeight, &lumex::ipc::action_handler::on_account_weight);
		handlers.emplace (lumexapi::Message::Message_ServiceRegister, &lumex::ipc::action_handler::on_service_register);
		handlers.emplace (lumexapi::Message::Message_ServiceStop, &lumex::ipc::action_handler::on_service_stop);
		handlers.emplace (lumexapi::Message::Message_TopicServiceStop, &lumex::ipc::action_handler::on_topic_service_stop);
	}
	return handlers;
}

lumex::ipc::action_handler::action_handler (lumex::node & node_a, lumex::ipc::ipc_server & server_a, std::weak_ptr<lumex::ipc::subscriber> const & subscriber_a, std::shared_ptr<flatbuffers::FlatBufferBuilder> const & builder_a) :
	flatbuffer_producer (builder_a),
	node (node_a),
	ipc_server (server_a),
	subscriber (subscriber_a)
{
}

void lumex::ipc::action_handler::on_topic_confirmation (lumexapi::Envelope const & envelope_a)
{
	auto confirmationTopic (get_message<lumexapi::TopicConfirmation> (envelope_a));
	ipc_server.get_broker ()->subscribe (subscriber, std::move (confirmationTopic));
	lumexapi::EventAckT ack;
	create_response (ack);
}

void lumex::ipc::action_handler::on_service_register (lumexapi::Envelope const & envelope_a)
{
	require_oneof (envelope_a, { lumex::ipc::access_permission::api_service_register, lumex::ipc::access_permission::service });
	auto query (get_message<lumexapi::ServiceRegister> (envelope_a));
	ipc_server.get_broker ()->service_register (query->service_name, this->subscriber);
	lumexapi::SuccessT success;
	create_response (success);
}

void lumex::ipc::action_handler::on_service_stop (lumexapi::Envelope const & envelope_a)
{
	require_oneof (envelope_a, { lumex::ipc::access_permission::api_service_stop, lumex::ipc::access_permission::service });
	auto query (get_message<lumexapi::ServiceStop> (envelope_a));
	if (query->service_name == "node")
	{
		ipc_server.node.stop ();
	}
	else
	{
		ipc_server.get_broker ()->service_stop (query->service_name);
	}
	lumexapi::SuccessT success;
	create_response (success);
}

void lumex::ipc::action_handler::on_topic_service_stop (lumexapi::Envelope const & envelope_a)
{
	auto topic (get_message<lumexapi::TopicServiceStop> (envelope_a));
	ipc_server.get_broker ()->subscribe (subscriber, std::move (topic));
	lumexapi::EventAckT ack;
	create_response (ack);
}

void lumex::ipc::action_handler::on_account_weight (lumexapi::Envelope const & envelope_a)
{
	require_oneof (envelope_a, { lumex::ipc::access_permission::api_account_weight, lumex::ipc::access_permission::account_query });
	bool is_deprecated_format{ false };
	auto query (get_message<lumexapi::AccountWeight> (envelope_a));
	auto balance (node.weight (parse_account (query->account, is_deprecated_format)));

	lumexapi::AccountWeightResponseT response;
	response.voting_weight = balance.str ();
	create_response (response);
}

void lumex::ipc::action_handler::on_is_alive (lumexapi::Envelope const & envelope)
{
	lumexapi::IsAliveT alive;
	create_response (alive);
}

bool lumex::ipc::action_handler::has_access (lumexapi::Envelope const & envelope_a, lumex::ipc::access_permission permission_a) const noexcept
{
	// If credentials are missing in the envelope, the default user is used
	std::string credentials;
	if (envelope_a.credentials () != nullptr)
	{
		credentials = envelope_a.credentials ()->str ();
	}

	return ipc_server.get_access ().has_access (credentials, permission_a);
}

bool lumex::ipc::action_handler::has_access_to_all (lumexapi::Envelope const & envelope_a, std::initializer_list<lumex::ipc::access_permission> permissions_a) const noexcept
{
	// If credentials are missing in the envelope, the default user is used
	std::string credentials;
	if (envelope_a.credentials () != nullptr)
	{
		credentials = envelope_a.credentials ()->str ();
	}

	return ipc_server.get_access ().has_access_to_all (credentials, permissions_a);
}

bool lumex::ipc::action_handler::has_access_to_oneof (lumexapi::Envelope const & envelope_a, std::initializer_list<lumex::ipc::access_permission> permissions_a) const noexcept
{
	// If credentials are missing in the envelope, the default user is used
	std::string credentials;
	if (envelope_a.credentials () != nullptr)
	{
		credentials = envelope_a.credentials ()->str ();
	}

	return ipc_server.get_access ().has_access_to_oneof (credentials, permissions_a);
}

void lumex::ipc::action_handler::require (lumexapi::Envelope const & envelope_a, lumex::ipc::access_permission permission_a) const
{
	if (!has_access (envelope_a, permission_a))
	{
		throw lumex::error (lumex::error_common::access_denied);
	}
}

void lumex::ipc::action_handler::require_all (lumexapi::Envelope const & envelope_a, std::initializer_list<lumex::ipc::access_permission> permissions_a) const
{
	if (!has_access_to_all (envelope_a, permissions_a))
	{
		throw lumex::error (lumex::error_common::access_denied);
	}
}

void lumex::ipc::action_handler::require_oneof (lumexapi::Envelope const & envelope_a, std::initializer_list<lumex::ipc::access_permission> permissions_a) const
{
	if (!has_access_to_oneof (envelope_a, permissions_a))
	{
		throw lumex::error (lumex::error_common::access_denied);
	}
}
