#include <lumex/lib/enum_flags_templ.hpp>
#include <lumex/lib/enum_util.hpp>
#include <lumex/lib/node_capabilities.hpp>

std::string_view lumex::to_string (lumex::node_capabilities value)
{
	return lumex::enum_to_string (value);
}

template std::ostream & lumex::operator<< <lumex::node_capabilities> (std::ostream &, lumex::node_capabilities_flags const &);
template std::string lumex::to_string<lumex::node_capabilities> (lumex::node_capabilities_flags const &);
