#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/messages/message.hpp>
#include <lumex/messages/message_type.hpp>

namespace lumex::messages
{
class bulk_pull_account final : public message
{
public:
	explicit bulk_pull_account (lumex::network_constants const & constants);
	bulk_pull_account (bool &, lumex::stream &, message_header const &);
	void serialize (lumex::stream &) const override;
	bool deserialize (lumex::stream &);
	void visit (message_visitor &) const override;
	lumex::account account;
	lumex::amount minimum_amount;
	bulk_pull_account_flags flags;
	static std::size_t constexpr size = sizeof (account) + sizeof (minimum_amount) + sizeof (bulk_pull_account_flags);

public: // Logging
	void operator() (lumex::object_stream &) const override;
};
}
