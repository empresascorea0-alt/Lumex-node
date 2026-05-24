#include <lumex/messages/asc_pull.hpp>
#include <lumex/node/bootstrap/bootstrap_context.hpp>
#include <lumex/node/bootstrap/bootstrap_service.hpp>

#include <boost/property_tree/ptree.hpp>

lumex::bootstrap_service::bootstrap_service (lumex::node_config const & config_a, lumex::ledger & ledger_a, lumex::ledger_notifications & ledger_notifications_a,
lumex::block_processor & block_processor_a, lumex::network & network_a, lumex::stats & stats_a, lumex::logger & logger_a) :
	config{ config_a },
	ledger{ ledger_a },
	ledger_notifications{ ledger_notifications_a },
	block_processor{ block_processor_a },
	network{ network_a },
	stats{ stats_a },
	logger{ logger_a },
	ctx_impl{ std::make_unique<lumex::bootstrap::bootstrap_context> (config_a, ledger_a, ledger_notifications_a, block_processor_a, network_a, stats_a, logger_a) },
	ctx{ *ctx_impl }
{
}

lumex::bootstrap_service::~bootstrap_service ()
{
}

void lumex::bootstrap_service::start ()
{
	ctx.start ();
}

void lumex::bootstrap_service::stop ()
{
	ctx.stop ();
}

void lumex::bootstrap_service::process (lumex::messages::asc_pull_ack const & message, std::shared_ptr<lumex::transport::channel> const & channel)
{
	ctx.process (message, channel);
}

void lumex::bootstrap_service::reset ()
{
	ctx.reset ();
}

void lumex::bootstrap_service::prioritize (lumex::account const & account)
{
	lumex::lock_guard<lumex::mutex> lock{ ctx.mutex };
	ctx.accounts.priority_set (account);
}

std::size_t lumex::bootstrap_service::priority_size () const
{
	lumex::lock_guard<lumex::mutex> lock{ ctx.mutex };
	return ctx.accounts.priority_size ();
}

std::size_t lumex::bootstrap_service::blocked_size () const
{
	lumex::lock_guard<lumex::mutex> lock{ ctx.mutex };
	return ctx.accounts.blocked_size ();
}

std::size_t lumex::bootstrap_service::score_size () const
{
	lumex::lock_guard<lumex::mutex> lock{ ctx.mutex };
	return ctx.scoring.size ();
}

bool lumex::bootstrap_service::prioritized (lumex::account const & account) const
{
	lumex::lock_guard<lumex::mutex> lock{ ctx.mutex };
	return ctx.accounts.prioritized (account);
}

bool lumex::bootstrap_service::blocked (lumex::account const & account) const
{
	lumex::lock_guard<lumex::mutex> lock{ ctx.mutex };
	return ctx.accounts.blocked (account);
}

boost::property_tree::ptree lumex::bootstrap_service::info () const
{
	lumex::unique_lock<lumex::mutex> lock{ ctx.mutex };

	// Copy the data under the lock, then serialize outside it
	auto [blocking, priorities] = ctx.accounts.info ();

	lock.unlock ();

	boost::property_tree::ptree result;

	// Priorities
	{
		boost::property_tree::ptree entries;
		for (auto const & entry : priorities)
		{
			boost::property_tree::ptree entry_l;
			entry_l.put ("account", entry.account.to_account ());
			entry_l.put ("priority", entry.priority);
			entries.push_back (std::make_pair ("", entry_l));
		}
		result.add_child ("priorities", entries);
	}
	// Blocking
	{
		boost::property_tree::ptree entries;
		for (auto const & entry : blocking)
		{
			boost::property_tree::ptree entry_l;
			entry_l.put ("account", entry.account.to_account ());
			entry_l.put ("dependency", entry.dependency.to_string ());
			entry_l.put ("dependency_account", entry.dependency_account.to_account ());
			entries.push_back (std::make_pair ("", entry_l));
		}
		result.add_child ("blocking", entries);
	}

	return result;
}

auto lumex::bootstrap_service::status () const -> status_result
{
	lumex::lock_guard<lumex::mutex> lock{ ctx.mutex };
	return {
		.priorities = ctx.accounts.priority_size (),
		.blocking = ctx.accounts.blocked_size (),
	};
}

lumex::container_info lumex::bootstrap_service::container_info () const
{
	return ctx.container_info ();
}
