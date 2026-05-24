#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/messages/message.hpp>

#include <cstdint>
#include <deque>
#include <memory>
#include <utility>
#include <variant>

namespace lumex::messages
{
/**
 * Type of requested asc pull data
 */
enum class asc_pull_type : uint8_t
{
	invalid = 0x0,
	blocks = 0x1,
	account_info = 0x2,
	frontiers = 0x3,
};

/**
 * Ascending bootstrap pull request
 */
class asc_pull_req final : public message
{
public:
	using id_t = uint64_t;

	explicit asc_pull_req (lumex::network_constants const &);
	asc_pull_req (bool & error, lumex::stream &, message_header const &);

	void serialize (lumex::stream &) const override;
	bool deserialize (lumex::stream &);
	void visit (message_visitor &) const override;

	static std::size_t size (message_header const &);

	/**
	 * Update payload size stored in header
	 * IMPORTANT: Must be called after any update to the payload
	 */
	void update_header ();

	void serialize_payload (lumex::stream &) const;
	void deserialize_payload (lumex::stream &);

private: // Debug
	/**
	 * Asserts that payload type is consistent with actual payload
	 */
	bool verify_consistency () const;

public: // Payload definitions
	enum class hash_type : uint8_t
	{
		account = 0,
		block = 1,
	};

	struct blocks_payload
	{
		void serialize (lumex::stream &) const;
		void deserialize (lumex::stream &);

	public: // Payload
		lumex::hash_or_account start{ 0 };
		uint8_t count{ 0 };
		hash_type start_type{};

	public: // Logging
		void operator() (lumex::object_stream &) const;
	};

	struct account_info_payload
	{
		void serialize (lumex::stream &) const;
		void deserialize (lumex::stream &);

	public: // Payload
		lumex::hash_or_account target{ 0 };
		hash_type target_type{};

	public: // Logging
		void operator() (lumex::object_stream &) const;
	};

	struct frontiers_payload
	{
		void serialize (lumex::stream &) const;
		void deserialize (lumex::stream &);

	public: // Payload
		lumex::account start{ 0 };
		uint16_t count{ 0 };

	public: // Logging
		void operator() (lumex::object_stream &) const;
	};

public: // Payload
	asc_pull_type type{ asc_pull_type::invalid };
	id_t id{ 0 };

	/** Payload depends on `asc_pull_type` */
	std::variant<empty_payload, blocks_payload, account_info_payload, frontiers_payload> payload;

public:
	/** Size of message without payload */
	constexpr static std::size_t partial_size = sizeof (type) + sizeof (id);

public: // Logging
	void operator() (lumex::object_stream &) const override;
};

/**
 * Ascending bootstrap pull response
 */
class asc_pull_ack final : public message
{
public:
	using id_t = asc_pull_req::id_t;

	explicit asc_pull_ack (lumex::network_constants const &);
	asc_pull_ack (bool & error, lumex::stream &, message_header const &);

	void serialize (lumex::stream &) const override;
	bool deserialize (lumex::stream &);
	void visit (message_visitor &) const override;

	static std::size_t size (message_header const &);

	/**
	 * Update payload size stored in header
	 * IMPORTANT: Must be called after any update to the payload
	 */
	void update_header ();

	void serialize_payload (lumex::stream &) const;
	void deserialize_payload (lumex::stream &);

private: // Debug
	/**
	 * Asserts that payload type is consistent with actual payload
	 */
	bool verify_consistency () const;

public: // Payload definitions
	struct blocks_payload
	{
		/* Header allows for 16 bit extensions; 65536 bytes / 500 bytes (block size with some future margin) ~ 131 */
		constexpr static std::size_t max_blocks = 128;

		void serialize (lumex::stream &) const;
		void deserialize (lumex::stream &);

	public: // Payload
		std::deque<std::shared_ptr<lumex::block>> blocks;

	public: // Logging
		void operator() (lumex::object_stream &) const;
	};

	struct account_info_payload
	{
		void serialize (lumex::stream &) const;
		void deserialize (lumex::stream &);

	public: // Payload
		lumex::account account{ 0 };
		lumex::block_hash account_open{ 0 };
		lumex::block_hash account_head{ 0 };
		uint64_t account_block_count{ 0 };
		lumex::block_hash account_conf_frontier{ 0 };
		uint64_t account_conf_height{ 0 };

	public: // Logging
		void operator() (lumex::object_stream &) const;
	};

	struct frontiers_payload
	{
		/* Header allows for 16 bit extensions; 65536 bytes / 64 bytes (account + frontier) ~ 1024, but we need some space for null frontier terminator */
		constexpr static std::size_t max_frontiers = 1000;

		using frontier = std::pair<lumex::account, lumex::block_hash>;

		void serialize (lumex::stream &) const;
		void deserialize (lumex::stream &);

		static void serialize_frontier (lumex::stream &, frontier const &);
		static frontier deserialize_frontier (lumex::stream &);

	public: // Payload
		std::deque<frontier> frontiers;

	public: // Logging
		void operator() (lumex::object_stream &) const;
	};

public: // Payload
	asc_pull_type type{ asc_pull_type::invalid };
	id_t id{ 0 };

	/** Payload depends on `asc_pull_type` */
	std::variant<empty_payload, blocks_payload, account_info_payload, frontiers_payload> payload;

public:
	/** Size of message without payload */
	constexpr static std::size_t partial_size = sizeof (type) + sizeof (id);

public: // Logging
	void operator() (lumex::object_stream &) const override;
};
}
