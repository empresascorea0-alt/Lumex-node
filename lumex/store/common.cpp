#include <lumex/lib/enum_util.hpp>
#include <lumex/store/common.hpp>

std::string_view lumex::store::to_string (lumex::store::open_mode mode)
{
	return lumex::enum_to_string (mode);
}