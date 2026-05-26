#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lumex
{
/**
 * Network variants with different genesis blocks and network parameters
 */
enum class network_type : uint16_t
{
	invalid = 0x0,
	// Low work parameters, publicly known genesis key, dev IP ports
	lumex_dev_network = 0x4c41, // 'L', 'A'
	// Normal work parameters, secret beta genesis key, beta IP ports
	lumex_beta_network = 0x4c42, // 'L', 'B'
	// Normal work parameters, secret live key, live IP ports
	lumex_live_network = 0x4c43, // 'L', 'C'
	// Normal work parameters, secret test genesis key, test IP ports
	lumex_test_network = 0x4c58, // 'L', 'X'
};

std::string_view to_string (lumex::network_type);
std::optional<lumex::network_type> parse_network (std::string);

/**
 * Current active network. Defaults to the compile-time option, but can be overridden.
 */
lumex::network_type get_active_network ();

/**
 * Optionally called on startup to override the global active network.
 * If not called, the compile-time option will be used.
 */
void set_active_network (lumex::network_type network);
}
