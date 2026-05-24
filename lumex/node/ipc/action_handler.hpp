#pragma once

#include <lumex/ipc_flatbuffers_lib/flatbuffer_producer.hpp>
#include <lumex/ipc_flatbuffers_lib/generated/flatbuffers/lumexapi_generated.h>
#include <lumex/node/ipc/ipc_access_config.hpp>

#include <functional>
#include <memory>
#include <unordered_map>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/idl.h>

namespace lumex
{
class error;
class node;
namespace ipc
{
	class ipc_server;
	class subscriber;

	/**
	 * Implements handlers for the various public IPC messages. When an action handler is completed,
	 * the flatbuffer contains the serialized response object.
	 * @note This is a light-weight class, and an instance can be created for every request.
	 */
	class action_handler final : public flatbuffer_producer, public std::enable_shared_from_this<action_handler>
	{
	public:
		action_handler (lumex::node & node, lumex::ipc::ipc_server & server, std::weak_ptr<lumex::ipc::subscriber> const & subscriber, std::shared_ptr<flatbuffers::FlatBufferBuilder> const & builder);

		void on_account_weight (lumexapi::Envelope const & envelope);
		void on_is_alive (lumexapi::Envelope const & envelope);
		void on_topic_confirmation (lumexapi::Envelope const & envelope);

		/** Request to register a service. The service name is associated with the current session. */
		void on_service_register (lumexapi::Envelope const & envelope);

		/** Request to stop a service by name */
		void on_service_stop (lumexapi::Envelope const & envelope);

		/** Subscribe to the ServiceStop event. The service must first have registered itself on the same session. */
		void on_topic_service_stop (lumexapi::Envelope const & envelope);

		/** Returns a mapping from api message types to handler functions */
		static auto handler_map () -> std::unordered_map<lumexapi::Message, std::function<void (action_handler *, lumexapi::Envelope const &)>, lumex::ipc::enum_hash>;

	private:
		bool has_access (lumexapi::Envelope const & envelope_a, lumex::ipc::access_permission permission_a) const noexcept;
		bool has_access_to_all (lumexapi::Envelope const & envelope_a, std::initializer_list<lumex::ipc::access_permission> permissions_a) const noexcept;
		bool has_access_to_oneof (lumexapi::Envelope const & envelope_a, std::initializer_list<lumex::ipc::access_permission> permissions_a) const noexcept;
		void require (lumexapi::Envelope const & envelope_a, lumex::ipc::access_permission permission_a) const;
		void require_all (lumexapi::Envelope const & envelope_a, std::initializer_list<lumex::ipc::access_permission> permissions_a) const;
		void require_oneof (lumexapi::Envelope const & envelope_a, std::initializer_list<lumex::ipc::access_permission> alternative_permissions_a) const;

		lumex::node & node;
		lumex::ipc::ipc_server & ipc_server;
		std::weak_ptr<lumex::ipc::subscriber> subscriber;
	};
}
}
