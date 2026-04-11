#pragma once

#include <nano/lib/network_types.hpp>

#include <fmt/ostream.h>

template <>
struct fmt::formatter<nano::endpoint> : fmt::ostream_formatter
{
};
template <>
struct fmt::formatter<nano::ip_address> : fmt::ostream_formatter
{
};
template <>
struct fmt::formatter<boost::asio::ip::address_v4> : fmt::ostream_formatter
{
};
