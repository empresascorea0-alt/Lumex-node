#pragma once

#include <lumex/lib/fwd.hpp>

#include <boost/property_tree/ptree.hpp>

#include <functional>
#include <string>

namespace lumex
{
class rpc_config;
class rpc_handler_interface;
class rpc_handler_request_params;

class rpc_handler : public std::enable_shared_from_this<lumex::rpc_handler>
{
public:
	rpc_handler (lumex::rpc_config const & rpc_config, std::string const & body_a, std::string const & request_id_a, std::function<void (std::string const &)> const & response_a, lumex::rpc_handler_interface & rpc_handler_interface_a, lumex::logger &);
	void process_request (lumex::rpc_handler_request_params const & request_params);

private:
	std::string body;
	std::string request_id;
	boost::property_tree::ptree request;
	std::function<void (std::string const &)> response;
	lumex::rpc_config const & rpc_config;
	lumex::rpc_handler_interface & rpc_handler_interface;
	lumex::logger & logger;
};
}
