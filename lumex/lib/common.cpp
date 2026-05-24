#include <lumex/lib/assert.hpp>
#include <lumex/lib/common.hpp>

#include <boost/algorithm/string/case_conv.hpp>

std::string lumex::to_string (lumex::database_backend const value)
{
	switch (value)
	{
		case lumex::database_backend::lmdb:
			return "lmdb";
		case lumex::database_backend::rocksdb:
			return "rocksdb";
	}
	release_assert (false);
}

std::optional<lumex::database_backend> lumex::parse_database_backend (std::string value)
{
	boost::algorithm::to_lower (value);

	if (value == "lmdb")
	{
		return lumex::database_backend::lmdb;
	}
	if (value == "rocksdb")
	{
		return lumex::database_backend::rocksdb;
	}
	return {};
}
