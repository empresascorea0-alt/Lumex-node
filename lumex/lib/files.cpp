#include <lumex/lib/config.hpp>
#include <lumex/lib/env.hpp>
#include <lumex/lib/files.hpp>

#include <boost/system/error_code.hpp>

#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string_view>
#include <thread>

#ifndef _WIN32
#include <sys/resource.h>
#endif

static std::vector<std::filesystem::path> all_unique_paths;

/*
 *
 */

lumex::file_descriptor_limits lumex::get_file_descriptor_limit ()
{
	lumex::file_descriptor_limits limits{
		.soft_limit = std::numeric_limits<std::size_t>::max (),
		.hard_limit = std::numeric_limits<std::size_t>::max (),
	};
#ifndef _WIN32
	rlimit limit{};
	if (getrlimit (RLIMIT_NOFILE, &limit) == 0)
	{
		limits.soft_limit = static_cast<std::size_t> (limit.rlim_cur);
		limits.hard_limit = static_cast<std::size_t> (limit.rlim_max);
	}
#endif
	return limits;
}

void lumex::set_file_descriptor_limit (std::size_t limit)
{
#ifndef _WIN32
	rlimit fd_limit{};
	if (-1 == getrlimit (RLIMIT_NOFILE, &fd_limit))
	{
		std::cerr << "WARNING: Unable to get current limits for the number of open file descriptors: " << std::strerror (errno);
		return;
	}

	if (fd_limit.rlim_cur >= limit)
	{
		return;
	}

	fd_limit.rlim_cur = std::min (static_cast<rlim_t> (limit), fd_limit.rlim_max);
	if (-1 == setrlimit (RLIMIT_NOFILE, &fd_limit))
	{
		std::cerr << "WARNING: Unable to set limits for the number of open file descriptors: " << std::strerror (errno);
		return;
	}
#endif
}

void lumex::initialize_file_descriptor_limit ()
{
	// Attempt to set the file descriptor limit to the maximum allowed value
	auto limits = lumex::get_file_descriptor_limit ();
	lumex::set_file_descriptor_limit (limits.hard_limit);

	auto updated_limits = lumex::get_file_descriptor_limit ();
	if (updated_limits.soft_limit < DEFAULT_FILE_DESCRIPTOR_LIMIT)
	{
		std::cerr << "WARNING: Current file descriptor limit of " << updated_limits.soft_limit << " (hard limit " << updated_limits.hard_limit << ")"
				  << " is lower than the " << DEFAULT_FILE_DESCRIPTOR_LIMIT << " recommended. Node was unable to change it." << std::endl;
	}
}

/*
 *
 */

void lumex::remove_all_files_in_dir (std::filesystem::path const & dir)
{
	for (auto & p : std::filesystem::directory_iterator (dir))
	{
		auto path = p.path ();
		if (std::filesystem::is_regular_file (path))
		{
			std::filesystem::remove (path);
		}
	}
}

void lumex::move_all_files_to_dir (std::filesystem::path const & from, std::filesystem::path const & to)
{
	for (auto & p : std::filesystem::directory_iterator (from))
	{
		auto path = p.path ();
		if (std::filesystem::is_regular_file (path))
		{
			std::filesystem::rename (path, to / path.filename ());
		}
	}
}

std::filesystem::path lumex::app_path ()
{
	static auto const path = [] () {
		if (auto value = lumex::env::get ("LUMEX_APP_PATH"))
		{
			std::cerr << "Application path overridden by LUMEX_APP_PATH environment variable: " << *value << std::endl;
			return std::filesystem::path{ *value };
		}
		return lumex::app_path_impl ();
	}();
	return path;
}

std::filesystem::path lumex::working_path (lumex::network_type network)
{
	auto result = lumex::app_path ();

	switch (network)
	{
		case lumex::network_type::invalid:
			release_assert (false);
			break;
		case lumex::network_type::lumex_dev_network:
			result /= "LumexDev";
			break;
		case lumex::network_type::lumex_beta_network:
			result /= "LumexBeta";
			break;
		case lumex::network_type::lumex_live_network:
			result /= "Lumex";
			break;
		case lumex::network_type::lumex_test_network:
			result /= "LumexTest";
			break;
	}
	return result;
}

std::filesystem::path lumex::random_filename ()
{
	std::random_device rd;
	std::mt19937 gen (rd ());
	std::uniform_int_distribution<> dis (0, 15);

	const char * hex_chars = "0123456789ABCDEF";
	std::string random_string;
	random_string.reserve (32);

	for (int i = 0; i < 32; ++i)
	{
		random_string += hex_chars[dis (gen)];
	}
	return std::filesystem::path{ random_string };
}

std::filesystem::path lumex::unique_path (lumex::network_type network)
{
	auto result = working_path (network) / random_filename ();

	std::filesystem::create_directories (result);

	all_unique_paths.push_back (result);
	return result;
}

void lumex::remove_temporary_directories ()
{
	for (auto & path : all_unique_paths)
	{
		boost::system::error_code ec;
		std::filesystem::remove_all (path, ec);
		if (ec)
		{
			std::cerr << "Could not remove temporary directory: " << ec.message () << std::endl;
		}
	}
}