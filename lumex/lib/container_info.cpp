#include <lumex/lib/container_info.hpp>

lumex::container_info_composite::container_info_composite (std::string name) :
	name (std::move (name))
{
}

bool lumex::container_info_composite::is_composite () const
{
	return true;
}

void lumex::container_info_composite::add_component (std::unique_ptr<container_info_component> child)
{
	children.push_back (std::move (child));
}

std::vector<std::unique_ptr<lumex::container_info_component>> const & lumex::container_info_composite::get_children () const
{
	return children;
}

std::string const & lumex::container_info_composite::get_name () const
{
	return name;
}

lumex::container_info_leaf::container_info_leaf (container_info_entry info) :
	info (std::move (info))
{
}

bool lumex::container_info_leaf::is_composite () const
{
	return false;
}

lumex::container_info_entry const & lumex::container_info_leaf::get_info () const
{
	return info;
}