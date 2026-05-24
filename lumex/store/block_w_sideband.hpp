#pragma once

#include <lumex/lib/block_sideband.hpp>

#include <memory>

namespace lumex::store
{
class block_w_sideband
{
public:
	std::shared_ptr<lumex::block> block;
	lumex::block_sideband sideband;
};

// Snapshot of block + sideband at v25 format which needs to be read for the v25 to v26 upgrade
class block_w_sideband_v25
{
public:
	std::shared_ptr<lumex::block> block;
	lumex::block_sideband_v25 sideband;
};
}
