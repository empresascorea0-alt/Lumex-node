#include <lumex/lib/assert.hpp>
#include <lumex/lib/files.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/stacktrace.hpp>

#include <boost/dll/runtime_symbol_info.hpp>

#include <fstream>
#include <iostream>

/*
 * Backing code for "release_assert" & "debug_assert", which are macros
 */
void assert_internal (char const * check_expr, char const * func, char const * file, unsigned int line, bool is_release_assert, std::string_view error_msg)
{
	std::stringstream ss;
	ss << "Assertion `" << check_expr << "` failed";
	if (!error_msg.empty ())
	{
		ss << ": " << error_msg;
	}
	ss << "\n";
	ss << file << ":" << line << " [" << func << "]"
	   << "'\n";

	// Output stack trace
	auto backtrace_str = lumex::generate_stacktrace ();
	ss << backtrace_str;

	// Output both to standard error and the default logger, so that the error info is persisted in the lumex specific log directory
	auto error_str = ss.str ();
	std::cerr << error_str << std::endl;
	lumex::default_logger ().critical (lumex::log::type::assert, "{}", error_str);

	// "abort" at the end of this function will go into any signal handlers (the daemon ones will generate a stack trace and load memory address files on non-Windows systems).
	// As there is no async-signal-safe way to generate stacktraces on Windows it must be done before aborting
#ifdef _WIN32
	{
		// Try construct the stacktrace dump in the same folder as the running executable, otherwise use the current directory.
		boost::system::error_code err;
		auto running_executable_filepath = boost::dll::program_location (err);
		std::string filename = is_release_assert ? "lumex_node_backtrace_release_assert.txt" : "lumex_node_backtrace_assert.txt";
		std::string filepath = filename;
		if (!err)
		{
			filepath = (running_executable_filepath.parent_path () / filename).string ();
		}

		std::ofstream file (filepath);
		lumex::set_secure_perm_file (filepath);
		file << backtrace_str;
	}
#endif

	abort ();
}