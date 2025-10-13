#pragma once

#include <cstdint>
#include <string_view>

/**
 * Build version information
 */
namespace nano
{
extern std::string_view const NANO_VERSION_STRING;
extern std::string_view const NANO_MAJOR_VERSION_STRING;
extern std::string_view const NANO_MINOR_VERSION_STRING;
extern std::string_view const NANO_PATCH_VERSION_STRING;
extern std::string_view const NANO_PRE_RELEASE_VERSION_STRING;
extern std::string_view const BUILD_INFO;

uint8_t get_major_node_version ();
uint8_t get_minor_node_version ();
uint8_t get_patch_node_version ();
uint8_t get_pre_release_node_version ();
}
