#pragma once

#include <lumex/lib/locks.hpp>
#include <lumex/node/endpoint.hpp>
#include <lumex/node/fwd.hpp>

#include <atomic>
#include <chrono>
#include <thread>

namespace lumex
{
class peer_history_config final
{
public:
	explicit peer_history_config (lumex::network_constants const & network);

	lumex::error deserialize (lumex::tomlconfig & toml);
	lumex::error serialize (lumex::tomlconfig & toml) const;

public:
	std::chrono::seconds erase_cutoff{ 60 * 60s };
	std::chrono::seconds check_interval{ 15s };
};

class peer_history final
{
public:
	peer_history (peer_history_config const &, lumex::store::ledger_store &, lumex::network &, lumex::logger &, lumex::stats &);
	~peer_history ();

	void start ();
	void stop ();

	std::vector<lumex::endpoint> peers () const;
	bool exists (lumex::endpoint const & endpoint) const;
	size_t size () const;
	void trigger ();

private:
	void run ();
	void run_one ();

private: // Dependencies
	peer_history_config const & config;
	lumex::store::ledger_store & store;
	lumex::network & network;
	lumex::logger & logger;
	lumex::stats & stats;

private:
	std::atomic<bool> stopped{ false };
	mutable lumex::mutex mutex;
	lumex::condition_variable condition;
	std::thread thread;
};
}
