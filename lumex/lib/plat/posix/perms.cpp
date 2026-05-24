#include <lumex/lib/files.hpp>
#include <lumex/lib/utility.hpp>

#include <sys/stat.h>
#include <sys/types.h>

void lumex::set_umask ()
{
	umask (077);
}

void lumex::set_secure_perm_directory (std::filesystem::path const & path)
{
	std::filesystem::permissions (path, std::filesystem::perms::owner_all);
}

void lumex::set_secure_perm_directory (std::filesystem::path const & path, std::error_code & ec)
{
	std::filesystem::permissions (path, std::filesystem::perms::owner_all, ec);
}

void lumex::set_secure_perm_file (std::filesystem::path const & path)
{
	std::filesystem::permissions (path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
}

void lumex::set_secure_perm_file (std::filesystem::path const & path, std::error_code & ec)
{
	std::filesystem::permissions (path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, ec);
}
