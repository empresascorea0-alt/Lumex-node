#include <lumex/lib/blocks.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/lib/vote.hpp>
#include <lumex/messages/confirm.hpp>
#include <lumex/node/network.hpp>
#include <lumex/node/transport/formatting.hpp>
#include <lumex/node/vote_replier.hpp>
#include <lumex/node/wallet.hpp>
#include <lumex/secure/ledger.hpp>
#include <lumex/secure/voting_policy.hpp>

lumex::vote_replier::vote_replier (vote_replier_config const & config_a, lumex::voting_policy & policy_a, lumex::ledger & ledger_a, lumex::wallet::wallets & wallets_a, lumex::network_constants const & network_constants_a, lumex::stats & stats_a, lumex::logger & logger_a, bool enable_voting) :
	config{ config_a },
	policy{ policy_a },
	ledger{ ledger_a },
	wallets{ wallets_a },
	network_constants{ network_constants_a },
	stats{ stats_a },
	logger{ logger_a },
	enabled{ enable_voting }
{
	queue.max_size_query = [this] (auto const & origin) {
		return config.channel_limit;
	};
	queue.priority_query = [] (auto const & origin) {
		return 1;
	};
}

lumex::vote_replier::~vote_replier ()
{
	debug_assert (threads.empty ());
}

void lumex::vote_replier::start ()
{
	debug_assert (threads.empty ());

	if (!enabled)
	{
		return;
	}

	for (auto i = 0; i < config.threads; ++i)
	{
		threads.emplace_back ([this] () {
			lumex::thread_role::set (lumex::thread_role::name::vote_replier);
			run ();
		});
	}
}

void lumex::vote_replier::stop ()
{
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
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

std::size_t lumex::vote_replier::size () const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	return queue.size ();
}

bool lumex::vote_replier::empty () const
{
	lumex::lock_guard<lumex::mutex> lock{ mutex };
	return queue.empty ();
}

bool lumex::vote_replier::request (request_type const & request, std::shared_ptr<lumex::transport::channel> const & channel)
{
	release_assert (channel != nullptr);
	debug_assert (!request.empty ());

	if (!enabled)
	{
		return false;
	}

	if (request.size () > lumex::network::confirm_ack_hashes_max)
	{
		stats.inc (lumex::stat::type::vote_replier, lumex::stat::detail::oversize);
		return false;
	}

	bool added = false;
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		added = queue.push ({ request, channel }, { lumex::no_value{}, channel });
	}
	if (added)
	{
		stats.inc (lumex::stat::type::vote_replier, lumex::stat::detail::request);
		stats.add (lumex::stat::type::vote_replier, lumex::stat::detail::request_hashes, request.size ());

		condition.notify_one ();
	}
	else
	{
		stats.inc (lumex::stat::type::vote_replier, lumex::stat::detail::overfill);
		stats.add (lumex::stat::type::vote_replier, lumex::stat::detail::overfill_hashes, request.size ());
	}

	return added;
}

void lumex::vote_replier::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (lumex::stat::type::vote_replier, lumex::stat::detail::loop);

		if (!queue.empty ())
		{
			if (queue.size () > lumex::queue_warning_threshold () && log_interval.elapse (15s))
			{
				logger.info (lumex::log::type::vote_replier, "{} requests in processing queue", queue.size ());
			}

			run_batch (lock);
			debug_assert (!lock.owns_lock ());
			lock.lock ();
		}
		else
		{
			condition.wait (lock, [&] { return stopped || !queue.empty (); });
		}
	}
}

void lumex::vote_replier::run_batch (lumex::unique_lock<lumex::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());
	debug_assert (!queue.empty ());

	debug_assert (config.batch_size > 0);
	auto batch = queue.next_batch (config.batch_size);

	lock.unlock ();

	auto transaction = ledger.tx_begin_read ();

	for (auto const & [value, origin] : batch)
	{
		auto const & [request, channel] = value;

		transaction.refresh_if_needed ();

		if (!channel->max (lumex::transport::traffic_type::vote_reply))
		{
			process (transaction, request, channel);
		}
		else
		{
			stats.inc (lumex::stat::type::vote_replier, lumex::stat::detail::channel_full, stat::dir::out);
		}
	}
}

void lumex::vote_replier::process (lumex::secure::transaction const & transaction, request_type const & request, std::shared_ptr<lumex::transport::channel> const & channel)
{
	debug_assert (request.size () <= lumex::network::confirm_ack_hashes_max);

	std::vector<lumex::vote_permit> permits;
	permits.reserve (request.size ());

	for (auto const & [hash, root] : request)
	{
		auto block = ledger.block_find (transaction, hash, root);
		if (block)
		{
			if (auto permit = policy.reply_final (transaction, *block))
			{
				permits.push_back (*permit);
				stats.inc (lumex::stat::type::vote_replier, lumex::stat::detail::reply_final);
				logger.debug (lumex::log::type::vote_replier, "Replying with final vote for: {} to: {}", block->hash (), channel);
			}
			else
			{
				stats.inc (lumex::stat::type::vote_replier, lumex::stat::detail::reply_skip);
				logger.debug (lumex::log::type::vote_replier, "Skipping reply for: {} to: {} due to voting policy", hash, channel);
			}
		}
		else
		{
			stats.inc (lumex::stat::type::vote_replier, lumex::stat::detail::reply_unknown);
			logger.debug (lumex::log::type::vote_replier, "Cannot reply for unknown block: {} to: {}", hash, channel);
		}
	}

	if (permits.empty ())
	{
		return;
	}

	stats.add (lumex::stat::type::vote_replier, lumex::stat::detail::reply_hashes, permits.size ());

	auto votes = policy.sign (lumex::vote_type::final, permits, wallets.signer ());

	for (auto const & vote : votes)
	{
		lumex::messages::confirm_ack msg{ network_constants, vote };
		channel->send (msg, lumex::transport::traffic_type::vote_reply, [this] (auto & ec, auto size) {
			stats.inc (lumex::stat::type::vote_replier_ec, to_stat_detail (ec), lumex::stat::dir::out);
		});
	}
}

lumex::container_info lumex::vote_replier::container_info () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	lumex::container_info info;
	info.add ("queue", queue.container_info ());
	return info;
}

/*
 * vote_replier_config
 */

lumex::error lumex::vote_replier_config::serialize (lumex::tomlconfig & toml) const
{
	toml.put ("channel_limit", channel_limit, "Maximum number of queued requests per channel. \ntype:uint64");
	toml.put ("threads", threads, "Number of threads for request processing. \ntype:uint64");
	toml.put ("batch_size", batch_size, "Number of requests to process in a single batch. \ntype:uint64");

	return toml.get_error ();
}

lumex::error lumex::vote_replier_config::deserialize (lumex::tomlconfig & toml)
{
	toml.get ("channel_limit", channel_limit);
	toml.get ("threads", threads);
	toml.get ("batch_size", batch_size);

	return toml.get_error ();
}
