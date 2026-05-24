#include <lumex/lib/formatting.hpp>
#include <lumex/lib/ratios.hpp>

#include <boost/multiprecision/cpp_int.hpp>

namespace lumex::log
{
std::ostream & operator<< (std::ostream & os, as_lumex_formatter const & wrapper)
{
	lumex::encode_balance (os, wrapper.value, lumex::lumex_ratio, wrapper.precision, true);
	return os;
}

std::ostream & operator<< (std::ostream & os, as_raw_lumex_formatter const & wrapper)
{
	os << wrapper.value;
	return os;
}
}
