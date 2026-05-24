#include <lumex/lib/version.hpp>

#include <boost/lexical_cast.hpp>
#include <boost/numeric/conversion/cast.hpp>

#define xstr(a) ver_str (a)
#define ver_str(a) #a

#ifndef TAG_VERSION_STRING
#error "TAG_VERSION_STRING must be defined"
#endif

#ifndef GIT_COMMIT_HASH
#define GIT_COMMIT_HASH UNKNOWN
#endif

namespace lumex
{
/**
 * Build version information - defined here to ensure consistent timestamps across all translation units
 */
std::string_view const LUMEX_VERSION_STRING{ xstr (TAG_VERSION_STRING) };
std::string_view const LUMEX_MAJOR_VERSION_STRING{ xstr (MAJOR_VERSION_STRING) };
std::string_view const LUMEX_MINOR_VERSION_STRING{ xstr (MINOR_VERSION_STRING) };
std::string_view const LUMEX_PATCH_VERSION_STRING{ xstr (PATCH_VERSION_STRING) };
std::string_view const LUMEX_PRE_RELEASE_VERSION_STRING{ xstr (PRE_RELEASE_VERSION_STRING) };

std::string_view const BUILD_INFO{ xstr (GIT_COMMIT_HASH BOOST_COMPILER) " \"BOOST " xstr (BOOST_VERSION) "\" BUILT " xstr (__DATE__) };

uint8_t get_major_node_version ()
{
	return boost::numeric_cast<uint8_t> (boost::lexical_cast<int> (LUMEX_MAJOR_VERSION_STRING));
}

uint8_t get_minor_node_version ()
{
	return boost::numeric_cast<uint8_t> (boost::lexical_cast<int> (LUMEX_MINOR_VERSION_STRING));
}

uint8_t get_patch_node_version ()
{
	return boost::numeric_cast<uint8_t> (boost::lexical_cast<int> (LUMEX_PATCH_VERSION_STRING));
}

uint8_t get_pre_release_node_version ()
{
	return boost::numeric_cast<uint8_t> (boost::lexical_cast<int> (LUMEX_PRE_RELEASE_VERSION_STRING));
}
}
