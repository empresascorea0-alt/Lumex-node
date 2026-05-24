#pragma once

#include <lumex/lib/function.hpp>
#include <lumex/lib/observer_set.hpp>
#include <lumex/node/block_context.hpp>
#include <lumex/node/fwd.hpp>
#include <lumex/secure/common.hpp>

#include <deque>
#include <functional>
#include <future>
#include <thread>

namespace lumex
{
class ledger_notifications
{
public: // Events
	// All processed blocks including forks, rejected etc
	using processed_batch_t = std::deque<std::pair<lumex::block_status, lumex::block_context>>;
	using processed_batch_event_t = lumex::observer_set<processed_batch_t>;
	processed_batch_event_t blocks_processed;

	// Rolled back blocks <rolled back blocks, root of rollback>
	using rolled_back_batch_t = std::deque<std::shared_ptr<lumex::block>>;
	using rolled_back_event_t = lumex::observer_set<std::deque<std::shared_ptr<lumex::block>>, lumex::qualified_root>;
	rolled_back_event_t blocks_rolled_back;

public:
	ledger_notifications (lumex::node_config const &, lumex::stats &, lumex::logger &);
	~ledger_notifications ();

	void start ();
	void stop ();

	/* Components should cooperate to ensure that the notification queue does not grow indefinitely */
	void wait (std::function<void ()> cooldown_action = nullptr);

	/*
	 * Write transactions are passed to ensure that notifications are queued in the correct order, which is the same as the order of write transactions
	 * However, we cannot dispatch notifications before the write transaction is committed otherwise the notified components may not see the changes
	 * It's an important subtlety and the reason for additional complexity in this and transaction classes
	 */
	void notify_processed (lumex::secure::write_transaction &, processed_batch_t batch, std::function<void ()> callback = nullptr);
	void notify_rolled_back (lumex::secure::write_transaction &, rolled_back_batch_t batch, lumex::qualified_root rollback_root, std::function<void ()> callback = nullptr);

	lumex::container_info container_info () const;

private: // Dependencies
	lumex::node_config const & config;
	lumex::stats & stats;
	lumex::logger & logger;

private:
	void run ();

private:
	using entry = std::pair<std::shared_future<void>, std::function<void ()>>; // <transaction commited future, notification callback>
	std::deque<entry> notifications;

	std::thread thread;
	lumex::condition_variable condition;
	mutable lumex::mutex mutex;
	bool stopped{ false };
};
}