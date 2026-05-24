#pragma once

#include <lumex/lib/interval.hpp>
#include <lumex/lib/locks.hpp>
#include <lumex/lib/threading.hpp>
#include <lumex/node/fair_queue_traits.hpp>
#include <lumex/node/fwd.hpp>

#include <thread>
#include <vector>

namespace lumex
{
class message_processor_config final
{
public:
	lumex::error deserialize (lumex::tomlconfig & toml);
	lumex::error serialize (lumex::tomlconfig & toml) const;

public:
	size_t threads{ std::clamp (lumex::hardware_concurrency () / 4, 1u, 2u) };
	size_t max_queue{ 64 };
};

/*
 * If mutex locking is ever a performance bottleneck, using a lock-free queue in front of the priority queue should be considered.
 */
class message_processor final
{
public:
	explicit message_processor (message_processor_config const &, lumex::node &);
	~message_processor ();

	void start ();
	void stop ();

	bool put (std::unique_ptr<lumex::messages::message>, std::shared_ptr<lumex::transport::channel> const &);
	void process (lumex::messages::message const &, std::shared_ptr<lumex::transport::channel> const &);

	lumex::container_info container_info () const;

private:
	void run ();
	void run_batch (lumex::unique_lock<lumex::mutex> &);

private: // Dependencies
	message_processor_config const & config;
	lumex::node & node;
	lumex::stats & stats;
	lumex::logger & logger;

private:
	using entry_t = std::pair<std::unique_ptr<lumex::messages::message>, std::shared_ptr<lumex::transport::channel>>;
	lumex::fair_queue<entry_t, lumex::no_value, std::shared_ptr<lumex::transport::channel>> queue;

	std::atomic<bool> stopped{ false };
	mutable lumex::mutex mutex;
	lumex::condition_variable condition;
	std::vector<std::thread> threads;

	lumex::interval log_interval;
};
}