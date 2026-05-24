#include <lumex/lib/stats.hpp>
#include <lumex/lib/thread_roles.hpp>
#include <lumex/node/ledger_notifications.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/secure/transaction.hpp>

lumex::ledger_notifications::ledger_notifications (lumex::node_config const & config, lumex::stats & stats, lumex::logger & logger) :
	config{ config },
	stats{ stats },
	logger{ logger }
{
}

lumex::ledger_notifications::~ledger_notifications ()
{
	debug_assert (!thread.joinable ());
}

void lumex::ledger_notifications::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		lumex::thread_role::set (lumex::thread_role::name::ledger_notifications);
		run ();
	} };
}

void lumex::ledger_notifications::stop ()
{
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

void lumex::ledger_notifications::wait (std::function<void ()> cooldown_action)
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	condition.wait (lock, [this, &cooldown_action] {
		bool predicate = stopped || notifications.size () < config.max_ledger_notifications;
		if (!predicate && cooldown_action)
		{
			cooldown_action ();
		}
		return predicate;
	});
}

void lumex::ledger_notifications::notify_processed (lumex::secure::write_transaction & transaction, processed_batch_t processed, std::function<void ()> callback)
{
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		notifications.emplace_back (transaction.get_future (), lumex::wrap_move_only ([this, processed = std::move (processed), callback = std::move (callback)] () mutable {
			stats.inc (lumex::stat::type::ledger_notifications, lumex::stat::detail::notify_processed);

			// Set results for futures when not holding the lock
			for (auto & [result, context] : processed)
			{
				if (context.callback)
				{
					context.callback (result);
				}
				context.set_result (result);
			}

			blocks_processed.notify (processed);

			if (callback)
			{
				callback ();
			}
		}));
	}
	condition.notify_all ();
}

void lumex::ledger_notifications::notify_rolled_back (lumex::secure::write_transaction & transaction, rolled_back_batch_t batch, lumex::qualified_root rollback_root, std::function<void ()> callback)
{
	{
		lumex::lock_guard<lumex::mutex> guard{ mutex };
		notifications.emplace_back (transaction.get_future (), lumex::wrap_move_only ([this, batch = std::move (batch), rollback_root, callback = std::move (callback)] () {
			stats.inc (lumex::stat::type::ledger_notifications, lumex::stat::detail::notify_rolled_back);

			blocks_rolled_back.notify (batch, rollback_root);

			if (callback)
			{
				callback ();
			}
		}));
	}
	condition.notify_all ();
}

void lumex::ledger_notifications::run ()
{
	lumex::unique_lock<lumex::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait (lock, [this] {
			return stopped || !notifications.empty ();
		});

		if (stopped)
		{
			return;
		}

		while (!notifications.empty ())
		{
			auto notification = std::move (notifications.front ());
			notifications.pop_front ();
			lock.unlock ();

			auto & [future, callback] = notification;
			future.wait (); // Wait for the associated transaction to be committed
			callback (); // Notify observers

			condition.notify_all (); // Notify waiting threads about possible vacancy

			lock.lock ();
		}
	}
}

lumex::container_info lumex::ledger_notifications::container_info () const
{
	lumex::lock_guard<lumex::mutex> guard{ mutex };

	lumex::container_info info;
	info.put ("notifications", notifications.size ());
	return info;
}