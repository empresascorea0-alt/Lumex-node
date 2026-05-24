#pragma once

#include <lumex/lib/network_types.hpp>

#include <fmt/ostream.h>

template <>
struct fmt::formatter<lumex::endpoint> : fmt::ostream_formatter
{
};
template <>
struct fmt::formatter<lumex::ip_address> : fmt::ostream_formatter
{
};
template <>
struct fmt::formatter<boost::asio::ip::address_v4> : fmt::ostream_formatter
{
};
