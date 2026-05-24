#pragma once

#include <lumex/node/transport/channel.hpp>
#include <lumex/node/transport/transport.hpp>

namespace lumex
{
namespace transport
{
	/**
	 * In-process transport channel. Mostly useful for unit tests
	 **/
	namespace inproc
	{
		class channel final : public lumex::transport::channel
		{
		public:
			explicit channel (lumex::node & node, lumex::node & destination);

			std::string to_string () const override;

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
				return lumex::transport::transport_type::loopback;
			}

			void close () override
			{
				// Can't be closed
			}

		protected:
			bool send_impl (lumex::messages::message const &, lumex::transport::traffic_type, lumex::transport::channel::callback_t) override;

		private:
			lumex::node & destination;
			lumex::endpoint const endpoint;
		};
	} // namespace inproc
} // namespace transport
} // namespace lumex
