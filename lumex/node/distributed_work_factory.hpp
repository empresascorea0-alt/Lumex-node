#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>

#include <atomic>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace lumex
{
class container_info_component;
class distributed_work;
class node;
class root;
struct work_request;

class distributed_work_factory final
{
public:
	distributed_work_factory (lumex::node &);
	~distributed_work_factory ();
	bool make (lumex::work_version const, lumex::root const &, std::vector<std::pair<std::string, uint16_t>> const &, uint64_t, std::function<void (std::optional<uint64_t>)> const &, std::optional<lumex::account> const & = std::nullopt);
	bool make (std::chrono::seconds const &, lumex::work_request const &);
	void cancel (lumex::root const &);
	void cleanup_finished ();
	void stop ();
	std::size_t size () const;
	lumex::container_info container_info () const;

private:
	std::unordered_multimap<lumex::root, std::weak_ptr<lumex::distributed_work>> items;

	lumex::node & node;
	mutable lumex::mutex mutex;
	std::atomic<bool> stopped{ false };
};
}
