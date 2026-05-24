#pragma once

#include <lumex/lib/common.hpp>
#include <lumex/lib/errors.hpp>
#include <lumex/lib/fwd.hpp>
#include <lumex/lib/keypair.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/messages/message.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace lumex::messages
{
enum class telemetry_maker : uint8_t
{
	nf_node = 0,
	nf_pruned_node = 1,
	lumex_node_light = 2,
	rs_lumex = 3
};

enum class telemetry_database_backend : uint8_t
{
	unknown = 0,
	lmdb = 1,
	rocksdb = 2
};

enum class telemetry_bootstrap_status : uint8_t
{
	unknown = 0,
	syncing = 1,
	synced = 2
};

// Tracks which set of fields are present in a telemetry payload
enum class telemetry_data_version : uint8_t
{
	v1 = 1,
	v2 = 2,
};

std::string to_string (telemetry_maker);
std::string to_string (telemetry_database_backend);
std::string to_string (telemetry_bootstrap_status);

telemetry_maker telemetry_maker_from_string (std::string const &);
telemetry_database_backend telemetry_database_backend_from_string (std::string const &);
telemetry_bootstrap_status telemetry_bootstrap_status_from_string (std::string const &);

telemetry_database_backend to_telemetry_database_backend (lumex::database_backend);

class telemetry_data
{
public: // Payload
	lumex::signature signature{ 0 };
	lumex::account node_id{};
	uint64_t block_count{ 0 };
	uint64_t cemented_count{ 0 };
	uint64_t unchecked_count{ 0 };
	uint64_t account_count{ 0 };
	uint64_t bandwidth_cap{ 0 };
	uint64_t uptime{ 0 };
	uint32_t peer_count{ 0 };
	uint8_t protocol_version{ 0 };
	lumex::block_hash genesis_block{ 0 };
	uint8_t major_version{ 0 };
	uint8_t minor_version{ 0 };
	uint8_t patch_version{ 0 };
	uint8_t pre_release_version{ 0 };
	telemetry_maker maker{ telemetry_maker::nf_node };
	std::chrono::system_clock::time_point timestamp;
	uint64_t active_difficulty{ 0 };
	telemetry_database_backend database_backend{ telemetry_database_backend::unknown };
	uint32_t confirmation_latency_ms_p50{ 0 };
	uint32_t confirmation_latency_ms_p90{ 0 };
	uint32_t confirmation_latency_ms_p99{ 0 };
	telemetry_bootstrap_status bootstrap_status{ telemetry_bootstrap_status::unknown };
	std::vector<uint8_t> unknown_data;

public:
	telemetry_data_version version{ telemetry_data_version::v2 };

public:
	void serialize (lumex::stream &) const;
	void deserialize (lumex::stream &, uint16_t);
	lumex::error serialize_json (lumex::jsonconfig &, bool) const;
	lumex::error deserialize_json (lumex::jsonconfig &, bool);
	void sign (lumex::keypair const &);
	bool validate_signature () const;
	bool operator== (telemetry_data const &) const;
	bool operator!= (telemetry_data const &) const;
	uint16_t serialized_size () const;

public:
	// Size does not include unknown_data
	static auto constexpr size_v1 = sizeof (signature) + sizeof (node_id) + sizeof (block_count) + sizeof (cemented_count) + sizeof (unchecked_count) + sizeof (account_count) + sizeof (bandwidth_cap) + sizeof (peer_count) + sizeof (protocol_version) + sizeof (uptime) + sizeof (genesis_block) + sizeof (major_version) + sizeof (minor_version) + sizeof (patch_version) + sizeof (pre_release_version) + sizeof (maker) + sizeof (uint64_t) + sizeof (active_difficulty);
	static auto constexpr size_v2 = size_v1 + sizeof (database_backend) + sizeof (confirmation_latency_ms_p50) + sizeof (confirmation_latency_ms_p90) + sizeof (confirmation_latency_ms_p99) + sizeof (bootstrap_status);
	static auto constexpr size = size_v2; // Current version size
	static auto constexpr latest_size = size; // This needs to be updated for each new telemetry version

private:
	static uint16_t size_for_version (telemetry_data_version);
	void serialize_without_signature (lumex::stream &) const;

public: // Logging
	void operator() (lumex::object_stream &) const;
};

class telemetry_req final : public message
{
public:
	explicit telemetry_req (lumex::network_constants const & constants);
	explicit telemetry_req (message_header const &);
	void serialize (lumex::stream &) const override;
	bool deserialize (lumex::stream &);
	void visit (message_visitor &) const override;

public: // Logging
	void operator() (lumex::object_stream &) const override;
};

class telemetry_ack final : public message
{
public:
	explicit telemetry_ack (lumex::network_constants const & constants);
	telemetry_ack (bool &, lumex::stream &, message_header const &);
	telemetry_ack (lumex::network_constants const & constants, telemetry_data const &);
	void serialize (lumex::stream &) const override;
	void visit (message_visitor &) const override;
	bool deserialize (lumex::stream &);
	uint16_t size () const;
	bool is_empty_payload () const;
	static uint16_t size (message_header const &);
	telemetry_data data;

public: // Logging
	void operator() (lumex::object_stream &) const override;
};
}
