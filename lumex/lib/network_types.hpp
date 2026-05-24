#pragma once

#include <lumex/boost/asio/ip/tcp.hpp>

namespace lumex
{
using ip_address = boost::asio::ip::address;
using endpoint = boost::asio::ip::basic_endpoint<boost::asio::ip::tcp>;
using tcp_endpoint = endpoint; // TODO: Remove this alias
}
