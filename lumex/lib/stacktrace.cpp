#include <lumex/boost/stacktrace.hpp>
#include <lumex/lib/stacktrace.hpp>

#include <sstream>

void lumex::dump_crash_stacktrace ()
{
	boost::stacktrace::safe_dump_to ("lumex_node_backtrace.dump");
}

std::string lumex::generate_stacktrace ()
{
	auto stacktrace = boost::stacktrace::stacktrace ();
	std::stringstream ss;
	ss << stacktrace;
	return ss.str ();
}
