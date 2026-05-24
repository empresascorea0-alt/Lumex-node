#include <lumex/lib/block_type.hpp>
#include <lumex/lib/enum_util.hpp>

std::string_view lumex::to_string (lumex::block_type type)
{
	return lumex::enum_to_string (type);
}
