#pragma once

#include <lumex/messages/asc_pull.hpp>
#include <lumex/messages/bulk_pull.hpp>
#include <lumex/messages/bulk_pull_account.hpp>
#include <lumex/messages/bulk_push.hpp>
#include <lumex/messages/confirm.hpp>
#include <lumex/messages/frontier_req.hpp>
#include <lumex/messages/keepalive.hpp>
#include <lumex/messages/node_id_handshake.hpp>
#include <lumex/messages/publish.hpp>
#include <lumex/messages/telemetry.hpp>

namespace lumex::messages
{
class message_visitor
{
public:
	virtual ~message_visitor () = default;

	virtual void keepalive (keepalive const & message)
	{
		default_handler (message);
	};
	virtual void publish (publish const & message)
	{
		default_handler (message);
	}
	virtual void confirm_req (confirm_req const & message)
	{
		default_handler (message);
	}
	virtual void confirm_ack (confirm_ack const & message)
	{
		default_handler (message);
	}
	virtual void bulk_pull (bulk_pull const & message)
	{
		default_handler (message);
	}
	virtual void bulk_pull_account (bulk_pull_account const & message)
	{
		default_handler (message);
	}
	virtual void bulk_push (bulk_push const & message)
	{
		default_handler (message);
	}
	virtual void frontier_req (frontier_req const & message)
	{
		default_handler (message);
	}
	virtual void node_id_handshake (node_id_handshake const & message)
	{
		default_handler (message);
	}
	virtual void telemetry_req (telemetry_req const & message)
	{
		default_handler (message);
	}
	virtual void telemetry_ack (telemetry_ack const & message)
	{
		default_handler (message);
	}
	virtual void asc_pull_req (asc_pull_req const & message)
	{
		default_handler (message);
	}
	virtual void asc_pull_ack (asc_pull_ack const & message)
	{
		default_handler (message);
	}
	virtual void default_handler (message const &){};
};
}
