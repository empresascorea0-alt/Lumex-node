#pragma once

#include <lumex/lib/observer_set.hpp>
#include <lumex/node/transport/channel.hpp>
#include <lumex/node/transport/fwd.hpp>

#include <atomic>
#include <functional>
#include <future>

namespace lumex::transport
{
class test_channel final : public lumex::transport::channel
{
public:
	using channel::channel;

	/// Register a callback invoked for each sent message
	void observe (std::function<void (lumex::messages::message const &, lumex::transport::traffic_type)> observer)
	{
		observers.add (std::move (observer));
	}

	/// Returns a future that resolves when the next message of type MessageT is sent
	template <typename MessageT>
	std::future<MessageT> observe ()
	{
		auto promise = std::make_shared<std::promise<MessageT>> ();
		auto future = promise->get_future ();
		auto fulfilled = std::make_shared<std::atomic<bool>> (false);

		observers.add ([promise, fulfilled] (lumex::messages::message const & message, lumex::transport::traffic_type) {
			if (auto * casted = dynamic_cast<MessageT const *> (&message))
			{
				if (!fulfilled->exchange (true))
				{
					promise->set_value (*casted);
				}
			}
		});

		return future;
	}

	/// Register a callback invoked for each sent message of type MessageT
	template <typename MessageT>
	void observe (std::function<void (MessageT const &)> observer)
	{
		observers.add ([observer] (lumex::messages::message const & message, lumex::transport::traffic_type) {
			if (auto * casted = dynamic_cast<MessageT const *> (&message))
			{
				observer (*casted);
			}
		});
	}

	lumex::endpoint get_remote_endpoint () const override
	{
		return {};
	}

	lumex::endpoint get_local_endpoint () const override
	{
		return {};
	}

	lumex::transport::transport_type get_type () const override
	{
		return lumex::transport::transport_type::loopback;
	}

	void close () override
	{
		// Can't be closed
	}

	std::string to_string () const override
	{
		return "test_channel";
	}

protected:
	bool send_impl (lumex::messages::message const & message, lumex::transport::traffic_type traffic_type, callback_t callback) override
	{
		observers.notify (message, traffic_type);

		if (callback)
		{
			callback (boost::system::errc::make_error_code (boost::system::errc::success), message.to_shared_const_buffer ().size ());
		}

		return true;
	}

private:
	lumex::observer_set<lumex::messages::message, lumex::transport::traffic_type> observers;
};
}
