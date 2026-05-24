#pragma once

#include <lumex/lib/keypair.hpp>
#include <lumex/lib/node_capabilities.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/messages/message.hpp>

#include <optional>
#include <variant>
#include <vector>

namespace lumex::messages
{
enum class handshake_version
{
	v1,
	v2,
	v3,
};

class node_id_handshake final : public message
{
public: // Payload definitions
	class query_payload
	{
	public:
		void serialize (lumex::stream &) const;
		void deserialize (lumex::stream &);

		static std::size_t constexpr size = sizeof (lumex::uint256_union);

	public:
		lumex::uint256_union cookie;

	public: // Logging
		void operator() (lumex::object_stream &) const;
	};

	class response_payload
	{
	public:
		void serialize (lumex::stream &) const;
		void deserialize (lumex::stream &, message_header const &);

		void sign (lumex::uint256_union const & cookie, lumex::keypair const &);
		bool validate (lumex::uint256_union const & cookie) const;

	private:
		std::vector<uint8_t> data_to_sign (lumex::uint256_union const & cookie) const;

	public:
		struct v2_payload
		{
			lumex::uint256_union salt;
			lumex::block_hash genesis;
		};

		struct v3_payload
		{
			lumex::uint256_union salt;
			lumex::block_hash genesis;
			lumex::node_capabilities_flags flags;
			uint64_t reserved{ 0 }; // Reserved for future use
		};

	public:
		lumex::account node_id;
		lumex::signature signature;
		std::variant<std::monostate, v2_payload, v3_payload> ext;

	public: // Accessors
		std::optional<lumex::block_hash> genesis () const;
		lumex::node_capabilities_flags flags () const;

	public:
		static std::size_t constexpr size_v1 = sizeof (lumex::account) + sizeof (lumex::signature);
		static std::size_t constexpr size_v2 = sizeof (lumex::account) + sizeof (lumex::signature) + sizeof (v2_payload);
		static std::size_t constexpr size_v3 = sizeof (lumex::account) + sizeof (lumex::signature) + sizeof (v3_payload);
		static std::size_t size (message_header const &);

	public: // Logging
		void operator() (lumex::object_stream &) const;
	};

public:
	explicit node_id_handshake (lumex::network_constants const &, std::optional<query_payload> query = std::nullopt, std::optional<response_payload> response = std::nullopt);
	node_id_handshake (bool &, lumex::stream &, message_header const &);

	void serialize (lumex::stream &) const override;
	bool deserialize (lumex::stream &);

	void visit (message_visitor &) const override;
	std::size_t size () const;
	static std::size_t size (message_header const &);

public: // Header
	static uint8_t constexpr query_flag = 0;
	static uint8_t constexpr response_flag = 1;
	static uint8_t constexpr v2_flag = 2;
	static uint8_t constexpr v3_flag = 3;

	static bool is_query (message_header const &);
	static bool is_response (message_header const &);
	static handshake_version version (message_header const &);
	handshake_version version () const;

public: // Payload
	std::optional<query_payload> query;
	std::optional<response_payload> response;

public: // Logging
	void operator() (lumex::object_stream &) const override;
};
}
