#pragma once

#include <lumex/node/transport/channel.hpp>
#include <lumex/node/transport/transport.hpp>

namespace lumex
{
namespace transport
{
	/**
	 * Fake channel that connects to nothing and allows its attributes to be manipulated. Mostly useful for unit tests.
	 **/
	namespace fake
	{
		class channel final : public lumex::transport::channel
		{
		public:
			explicit channel (lumex::node &);

			std::string to_string () const override;

			void set_endpoint (lumex::endpoint const & endpoint_a)
			{
				endpoint = endpoint_a;
			}

			lumex::endpoint get_remote_endpoint () const override
			{
				return endpoint;
			}

			lumex::endpoint get_local_endpoint () const override
			{
				return endpoint;
			}

			lumex::transport::transport_type get_type () const override
			{
				return lumex::transport::transport_type::fake;
			}

			void close () override
			{
				closed = true;
			}

			bool alive () const override
			{
				return !closed;
			}

		protected:
			bool send_impl (lumex::messages::message const &, lumex::transport::traffic_type, lumex::transport::channel::callback_t) override;

		private:
			lumex::endpoint endpoint;

			std::atomic<bool> closed{ false };
		};
	} // namespace fake
} // namespace transport
} // namespace lumex
