#include <lumex/messages/messages.hpp>
#include <lumex/node/common.hpp>
#include <lumex/node/testing.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace lumex
{
void force_lumex_dev_network ();
}
namespace
{
std::shared_ptr<lumex::test::system> system0;
std::shared_ptr<lumex::node> node0;

class fuzz_visitor : public lumex::messages::message_visitor
{
public:
	virtual void keepalive (lumex::messages::keepalive const &) override
	{
	}
	virtual void publish (lumex::messages::publish const &) override
	{
	}
	virtual void confirm_req (lumex::messages::confirm_req const &) override
	{
	}
	virtual void confirm_ack (lumex::messages::confirm_ack const &) override
	{
	}
	virtual void bulk_pull (lumex::messages::bulk_pull const &) override
	{
	}
	virtual void bulk_pull_account (lumex::messages::bulk_pull_account const &) override
	{
	}
	virtual void bulk_push (lumex::messages::bulk_push const &) override
	{
	}
	virtual void frontier_req (lumex::messages::frontier_req const &) override
	{
	}
	virtual void node_id_handshake (lumex::messages::node_id_handshake const &) override
	{
	}
	virtual void telemetry_req (lumex::messages::telemetry_req const &) override
	{
	}
	virtual void telemetry_ack (lumex::messages::telemetry_ack const &) override
	{
	}
};
}

/** Fuzz live message parsing. This covers parsing and block/vote uniquing. */
void fuzz_message_parser (uint8_t const * Data, size_t Size)
{
	static bool initialized = false;
	if (!initialized)
	{
		lumex::force_lumex_dev_network ();
		initialized = true;
		system0 = std::make_shared<lumex::test::system> (1);
		node0 = system0->nodes[0];
	}

	fuzz_visitor visitor;
	lumex::messages::message_parser parser (node0->network.filter, node0->block_uniquer, node0->vote_uniquer, visitor, node0->work);
	parser.deserialize_buffer (Data, Size);
}

/** Fuzzer entry point */
extern "C" int LLVMFuzzerTestOneInput (uint8_t const * Data, size_t Size)
{
	fuzz_message_parser (Data, Size);
	return 0;
}
