#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/store/fwd.hpp>

#include <cstdint>
#include <optional>

namespace lumex::store
{
enum class meta_key : uint64_t
{
	version = 1,
	topo_index_enabled = 8,
};

class meta_view
{
public:
	explicit meta_view (lumex::store::backend &);

	void put_version (lumex::store::write_transaction const &, uint64_t version);
	uint64_t get_version (lumex::store::transaction const &) const;
	bool version_exists (lumex::store::transaction const &) const;

	bool get_flag (lumex::store::transaction const &, lumex::store::meta_key) const;
	void put_flag (lumex::store::write_transaction const &, lumex::store::meta_key, bool value);

private:
	std::optional<lumex::uint256_union> get_value (lumex::store::transaction const &, lumex::store::meta_key) const;
	void put_value (lumex::store::write_transaction const &, lumex::store::meta_key, lumex::uint256_union const & value);

	lumex::store::backend & backend;
};
}
