#pragma once

#include <lumex/lib/assert.hpp>
#include <lumex/node/block_source.hpp>
#include <lumex/secure/common.hpp>

#include <future>

namespace lumex
{
class block_context
{
public:
	using result_t = lumex::block_status;
	using callback_t = std::function<void (result_t)>;

public: // Keep fields public for simplicity
	std::shared_ptr<lumex::block> block;
	lumex::block_source source;
	callback_t callback;
	std::chrono::steady_clock::time_point arrival{ std::chrono::steady_clock::now () };

public:
	block_context (std::shared_ptr<lumex::block> block, lumex::block_source source, callback_t callback = nullptr) :
		block{ std::move (block) },
		source{ source },
		callback{ std::move (callback) }
	{
		debug_assert (source != lumex::block_source::unknown);
	}

	std::future<result_t> get_future ()
	{
		return promise.get_future ();
	}

	void set_result (result_t result)
	{
		promise.set_value (result);
	}

private:
	std::promise<result_t> promise;
};
}