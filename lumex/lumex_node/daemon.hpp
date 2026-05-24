#include <lumex/lib/logging.hpp>

namespace lumex
{
class node_flags;

class daemon
{
	lumex::logger logger{ "daemon" };

public:
	void run (std::filesystem::path const & data_path, lumex::node_flags const &);
};
}
