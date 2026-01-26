#pragma once

#include <filesystem>
#include <string>

/**
 * Global registry for runtime files that should be cleaned up on process exit.
 * Files are automatically removed when the process exits, even via std::exit().
 */
namespace nano::runtime_files
{
/**
 * Creates a file with the given contents and registers it for cleanup on exit.
 * Creates parent directories if they don't exist.
 * @throws std::runtime_error if file creation fails
 */
void create (std::filesystem::path const & path, std::string const & contents);

/**
 * Removes all registered files. Called automatically on process exit.
 * Can also be called manually for testing.
 */
void cleanup ();

/**
 * Creates a PID file with the current process ID and registers it for cleanup on exit.
 * @throws std::runtime_error if file creation fails
 */
void create_pid_file (std::filesystem::path const & path);
}
