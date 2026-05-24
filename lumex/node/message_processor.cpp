#include <lumex/lib/logging.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/thread_roles.hpp>
#include <lumex/lib/vote.hpp>
#include <lumex/messages/messages.hpp>
#include <lumex/node/block_processor.hpp>
#include <lumex/node/bootstrap/bootstrap_server.hpp>
#include <lumex/node/bootstrap/bootstrap_service.hpp>
#include <lumex/node/message_processor.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/telemetry.hpp>
#include <lumex/node/vote_processor.hpp>
#include <lumex/node/vote_replier.hpp>
#include <lumex/node/wallet.hpp>

lumex::message_processor::message_processor (message_processor_config const & config_a, lumex::node & node_a) :
	config{ config_a },
	node{ node_a },
	stats{ node.stats },
	logger{ node.logger }
{
	queue.max_size_query = [this] (auto const & origin) {
		return config.max_queue;
	};

	queue.priority_query = [this] (auto const & origin) {
		return 1;
	};
}

lumex::message_processor::~message_processor ()
{
	debug_assert (threads.empty ());
}

void lumex::message_processor::start ()
{
	debug_assert (threads.empty ());

	for (int n = 0; n < config.threads; ++n)
	{
		threads.emplace_back ([this] () {
			lumex::thread_role::set (lumex::thread_role::name::message_processing);
			try
			{
				run ();
			}
			catch (boost::system::error_code & ec)
			{
				node.logger.critical (lumex::log::type::network, "Error: {}", ec.message ());
				release_assert (false);
			}
			catch (std::error_code & ec)
			{
				node.logger.critical (lumex::log::type::network, "Error: {}", ec.message ());
				release_assert (false);
			}
			catch (std::runtime_error & err)
			{
				node.logger.critical (lumex::log::type::network, "Error: {}", err.what ());
				release_assert (false);
			}
			catch (...)
			{
				node.logger.critical (lumex::log::type::network, "Unknown error");
				release_assert (false);
			}
		});
	}
}

void lumex::message_processor::stop ()
{
	{
		lumex::lock_guard<lumex::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();

	for (auto & thread : threads)
	{
		if (thread.joinable ())
		{
			thread.join ();
		}
	}
	threads.clear ();
}

bool lumex::message_processor::put (std::unique_ptr<lumex::messages::message> message, std::shared_ptr<lumex::transport::channel> const & channel)
{
	release_assert (message != nullptr);
	release_assert (channel != nullptr);

	auto const type = message->type ();

	bool added = false;
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		added = queue.push ({ std::move (message), channel }, { lumex::no_value{}, channel });
	}
	if (added)
	{
		stats.inc (lumex::stat::type::message_processor, lumex::stat::detail::process);
		stats.inc (lumex::stat::type::message_processor_type, to_stat_detail (type));

		condition.notify_all ();
	}
	else
	{
		stats.inc (lumex::stat::type::message_processor, lumex::stat::detail::overfill);
		stats.inc (lumex::stat::type::message_processor_overfill, to_stat_detail (type));
	}
	return added;
}

void lumex::message_processor::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (lumex::stat::type::message_processor, lumex::stat::detail::loop);

		if (!queue.empty ())
		{
			// Only log if component is under pressure
			if (queue.size () > lumex::queue_warning_threshold () && log_interval.elapse (15s))
			{
				logger.info (lumex::log::type::message_processor, "{} messages in processing queue", queue.size ());
			}

			run_batch (lock);
			debug_assert (!lock.owns_lock ());
			lock.lock ();
		}
		else
		{
			condition.wait (lock, [&] {
				return stopped || !queue.empty ();
			});
		}
	}
}

void lumex::message_processor::run_batch (lumex::unique_lock<lumex::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());
	debug_assert (!queue.empty ());

	lumex::timer<std::chrono::milliseconds> timer;
	timer.start ();

	size_t const max_batch_size = 1024 * 4;
	auto batch = queue.next_batch (max_batch_size);

	lock.unlock ();

	for (auto const & [entry, origin] : batch)
	{
		auto const & [message, channel] = entry;
		release_assert (message != nullptr);
		process (*message, channel);
	}

	if (timer.since_start () > std::chrono::milliseconds (100))
	{
		logger.debug (lumex::log::type::message_processor, "Processed {} messages in {} milliseconds (rate of {} messages per second)",
		batch.size (),
		timer.since_start ().count (),
		((batch.size () * 1000ULL) / timer.value ().count ()));
	}
}

namespace
{
class process_visitor : public lumex::messages::message_visitor
{
public:
	process_visitor (lumex::node & node_a, std::shared_ptr<lumex::transport::channel> const & channel_a) :
		node{ node_a },
		channel{ channel_a }
	{
	}

	void keepalive (lumex::messages::keepalive const & message) override
	{
		// Check for self reported peering port
		auto self_report = message.peers[0];
		if (self_report.address () == boost::asio::ip::address_v6{} && self_report.port () != 0)
		{
			// Remember this for future forwarding to other peers
			lumex::endpoint peering_endpoint{ channel->get_remote_endpoint ().address (), self_report.port () };
			channel->set_peering_endpoint (peering_endpoint);
		}
		channel->set_last_keepalive (message);
	}

	void publish (lumex::messages::publish const & message) override
	{
		// Put blocks that are being initially broadcasted in a separate queue, so that they won't have to compete with rebroadcasted blocks
		// Both queues have the same priority and size, so the potential for exploiting this is limited
		debug_assert (message.digest != 0 || channel->get_type () != lumex::transport::transport_type::tcp); // Messages received from the network should have their digest set
		bool added = node.block_processor.add (message.block, message.is_originator () ? lumex::block_source::live_originator : lumex::block_source::live, channel);
		if (!added)
		{
			node.network.filter.clear (message.digest);
			node.stats.inc (lumex::stat::type::message_drop, lumex::stat::detail::publish, lumex::stat::dir::in);
		}
	}

	void confirm_req (lumex::messages::confirm_req const & message) override
	{
		// Don't load nodes with disabled voting
		// TODO: This check should be cached somewhere
		if (node.config.enable_voting && node.wallets.reps ().voting > 0)
		{
			if (!message.roots_hashes.empty ())
			{
				node.vote_replier.request (message.roots_hashes, channel);
			}
		}
	}

	void confirm_ack (lumex::messages::confirm_ack const & message) override
	{
		// Ignore zero account votes
		if (message.vote->account.is_zero ())
		{
			node.stats.inc (lumex::stat::type::message_drop, lumex::stat::detail::confirm_ack_zero_account, lumex::stat::dir::in);
			return;
		}

		debug_assert (message.digest != 0 || channel->get_type () != lumex::transport::transport_type::tcp); // Messages received from the network should have their digest set
		bool added = node.vote_processor.vote (message.vote, channel, message.is_rebroadcasted () ? lumex::vote_source::rebroadcast : lumex::vote_source::live);
		if (!added)
		{
			node.network.filter.clear (message.digest);
			node.stats.inc (lumex::stat::type::message_drop, lumex::stat::detail::confirm_ack, lumex::stat::dir::in);
		}
	}

	void bulk_pull (lumex::messages::bulk_pull const &) override
	{
		debug_assert (false);
	}

	void bulk_pull_account (lumex::messages::bulk_pull_account const &) override
	{
		debug_assert (false);
	}

	void bulk_push (lumex::messages::bulk_push const &) override
	{
		debug_assert (false);
	}

	void frontier_req (lumex::messages::frontier_req const &) override
	{
		debug_assert (false);
	}

	void node_id_handshake (lumex::messages::node_id_handshake const & message) override
	{
		node.stats.inc (lumex::stat::type::message, lumex::stat::detail::node_id_handshake, lumex::stat::dir::in);
	}

	void telemetry_req (lumex::messages::telemetry_req const & message) override
	{
		// Ignore telemetry requests as telemetry is being periodically broadcasted since V25+
	}

	void telemetry_ack (lumex::messages::telemetry_ack const & message) override
	{
		node.telemetry.process (message, channel);
	}

	void asc_pull_req (lumex::messages::asc_pull_req const & message) override
	{
		node.bootstrap_server.request (message, channel);
	}

	void asc_pull_ack (lumex::messages::asc_pull_ack const & message) override
	{
		node.bootstrap.process (message, channel);
	}

private:
	lumex::node & node;
	std::shared_ptr<lumex::transport::channel> channel;
};
}

void lumex::message_processor::process (lumex::messages::message const & message, std::shared_ptr<lumex::transport::channel> const & channel)
{
	release_assert (channel != nullptr);

	debug_assert (message.header.network == node.network_params.network.current_network);
	debug_assert (message.header.version_using >= node.network_params.network.protocol_version_min);

	stats.inc (lumex::stat::type::message, to_stat_detail (message.type ()), lumex::stat::dir::in);
	logger.trace (lumex::log::type::message, to_log_detail (message.type ()), lumex::log::arg{ "message", message });

	process_visitor visitor{ node, channel };
	message.visit (visitor);
}

lumex::container_info lumex::message_processor::container_info () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	lumex::container_info info;
	info.add ("queue", queue.container_info ());
	return info;
}

/*
 * message_processor_config
 */

lumex::error lumex::message_processor_config::serialize (lumex::tomlconfig & toml) const
{
	toml.put ("threads", threads, "Number of threads to use for message processing. \ntype:uint64");
	toml.put ("max_queue", max_queue, "Maximum number of messages per peer to queue for processing. \ntype:uint64");

	return toml.get_error ();
}

lumex::error lumex::message_processor_config::deserialize (lumex::tomlconfig & toml)
{
	toml.get ("threads", threads);
	toml.get ("max_queue", max_queue);

	return toml.get_error ();
}
