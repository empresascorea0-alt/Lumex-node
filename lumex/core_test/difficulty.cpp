#include <lumex/lib/block_type.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/config.hpp>
#include <lumex/lib/epoch.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/work.hpp>
#include <lumex/lib/work_version.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/network_params.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

TEST (difficulty, multipliers)
{
	{
		uint64_t base = 0xff00000000000000;
		uint64_t difficulty = 0xfff27e7a57c285cd;
		double expected_multiplier = 18.95461493377003;

		ASSERT_NEAR (expected_multiplier, lumex::difficulty::to_multiplier (difficulty, base), 1e-10);
		ASSERT_EQ (difficulty, lumex::difficulty::from_multiplier (expected_multiplier, base));
	}

	{
		uint64_t base = 0xffffffc000000000;
		uint64_t difficulty = 0xfffffe0000000000;
		double expected_multiplier = 0.125;

		ASSERT_NEAR (expected_multiplier, lumex::difficulty::to_multiplier (difficulty, base), 1e-10);
		ASSERT_EQ (difficulty, lumex::difficulty::from_multiplier (expected_multiplier, base));
	}

	{
		constexpr uint64_t base = std::numeric_limits<std::uint64_t>::max ();
		uint64_t difficulty = 0xffffffffffffff00;
		double expected_multiplier = 0.00390625;

		ASSERT_NEAR (expected_multiplier, lumex::difficulty::to_multiplier (difficulty, base), 1e-10);
		ASSERT_EQ (difficulty, lumex::difficulty::from_multiplier (expected_multiplier, base));
	}

	{
		uint64_t base = 0x8000000000000000;
		uint64_t difficulty = 0xf000000000000000;
		double expected_multiplier = 8.0;

		ASSERT_NEAR (expected_multiplier, lumex::difficulty::to_multiplier (difficulty, base), 1e-10);
		ASSERT_EQ (difficulty, lumex::difficulty::from_multiplier (expected_multiplier, base));
	}

	{
		uint64_t base = 0xffffffc000000000;
		uint64_t difficulty_nil = 0;
		double multiplier_nil = 0.;

		ASSERT_EQ (0., lumex::difficulty::to_multiplier (difficulty_nil, base));
		ASSERT_EQ (0, lumex::difficulty::from_multiplier (multiplier_nil, base));
	}
}

TEST (difficulty, overflow)
{
	// Overflow max (attempt to overflow & receive lower difficulty)
	{
		constexpr uint64_t base = std::numeric_limits<std::uint64_t>::max (); // Max possible difficulty
		constexpr uint64_t difficulty = std::numeric_limits<std::uint64_t>::max ();
		double multiplier = 1.001; // Try to increase difficulty above max

		ASSERT_EQ (difficulty, lumex::difficulty::from_multiplier (multiplier, base));
	}

	// Overflow min (attempt to overflow & receive higher difficulty)
	{
		uint64_t base = 1; // Min possible difficulty before 0
		uint64_t difficulty = 0;
		double multiplier = 0.999; // Decrease difficulty

		ASSERT_EQ (difficulty, lumex::difficulty::from_multiplier (multiplier, base));
	}
}

TEST (difficulty, zero)
{
	// Tests with base difficulty 0 should return 0 with any multiplier
	{
		uint64_t base = 0; // Min possible difficulty
		uint64_t difficulty = 0;
		double multiplier = 0.000000001; // Decrease difficulty

		ASSERT_EQ (difficulty, lumex::difficulty::from_multiplier (multiplier, base));
	}

	{
		uint64_t base = 0; // Min possible difficulty
		uint64_t difficulty = 0;
		double multiplier = 1000000000.0; // Increase difficulty

		ASSERT_EQ (difficulty, lumex::difficulty::from_multiplier (multiplier, base));
	}
}

TEST (difficulty, network_constants)
{
	auto & full_thresholds = lumex::work_thresholds::publish_full;
	auto & beta_thresholds = lumex::work_thresholds::publish_beta;
	auto & dev_thresholds = lumex::work_thresholds::publish_dev;

	ASSERT_NEAR (8., lumex::difficulty::to_multiplier (full_thresholds.epoch_2, full_thresholds.epoch_1), 1e-10);
	ASSERT_NEAR (1 / 8., lumex::difficulty::to_multiplier (full_thresholds.epoch_2_receive, full_thresholds.epoch_1), 1e-10);
	ASSERT_NEAR (1., lumex::difficulty::to_multiplier (full_thresholds.epoch_2_receive, full_thresholds.entry), 1e-10);
	ASSERT_NEAR (1., lumex::difficulty::to_multiplier (full_thresholds.epoch_2, full_thresholds.base), 1e-10);

	ASSERT_NEAR (1 / 64., lumex::difficulty::to_multiplier (beta_thresholds.epoch_1, full_thresholds.epoch_1), 1e-10);
	ASSERT_NEAR (1., lumex::difficulty::to_multiplier (beta_thresholds.epoch_2, beta_thresholds.epoch_1), 1e-10);
	ASSERT_NEAR (1 / 2., lumex::difficulty::to_multiplier (beta_thresholds.epoch_2_receive, beta_thresholds.epoch_1), 1e-10);
	ASSERT_NEAR (1., lumex::difficulty::to_multiplier (beta_thresholds.epoch_2_receive, beta_thresholds.entry), 1e-10);
	ASSERT_NEAR (1., lumex::difficulty::to_multiplier (beta_thresholds.epoch_2, beta_thresholds.base), 1e-10);

	ASSERT_NEAR (8., lumex::difficulty::to_multiplier (dev_thresholds.epoch_2, dev_thresholds.epoch_1), 1e-10);
	ASSERT_NEAR (1 / 8., lumex::difficulty::to_multiplier (dev_thresholds.epoch_2_receive, dev_thresholds.epoch_1), 1e-10);
	ASSERT_NEAR (1., lumex::difficulty::to_multiplier (dev_thresholds.epoch_2_receive, dev_thresholds.entry), 1e-10);
	ASSERT_NEAR (1., lumex::difficulty::to_multiplier (dev_thresholds.epoch_2, dev_thresholds.base), 1e-10);

	lumex::work_version version{ lumex::work_version::work_1 };
	ASSERT_EQ (lumex::dev::network_params.work.base, lumex::dev::network_params.work.epoch_2);
	ASSERT_EQ (lumex::dev::network_params.work.base, lumex::dev::network_params.work.threshold_base (version));
	ASSERT_EQ (lumex::dev::network_params.work.entry, lumex::dev::network_params.work.threshold_entry (version, lumex::block_type::state));
	ASSERT_EQ (lumex::dev::network_params.work.epoch_1, lumex::dev::network_params.work.threshold_entry (version, lumex::block_type::send));
	ASSERT_EQ (lumex::dev::network_params.work.epoch_1, lumex::dev::network_params.work.threshold_entry (version, lumex::block_type::receive));
	ASSERT_EQ (lumex::dev::network_params.work.epoch_1, lumex::dev::network_params.work.threshold_entry (version, lumex::block_type::open));
	ASSERT_EQ (lumex::dev::network_params.work.epoch_1, lumex::dev::network_params.work.threshold_entry (version, lumex::block_type::change));
	ASSERT_EQ (lumex::dev::network_params.work.epoch_1, lumex::dev::network_params.work.threshold (version, lumex::block_details (lumex::epoch::epoch_0, false, false, false)));
	ASSERT_EQ (lumex::dev::network_params.work.epoch_1, lumex::dev::network_params.work.threshold (version, lumex::block_details (lumex::epoch::epoch_1, false, false, false)));
	ASSERT_EQ (lumex::dev::network_params.work.epoch_1, lumex::dev::network_params.work.threshold (version, lumex::block_details (lumex::epoch::epoch_1, false, false, false)));

	// Send [+ change]
	ASSERT_EQ (lumex::dev::network_params.work.epoch_2, lumex::dev::network_params.work.threshold (version, lumex::block_details (lumex::epoch::epoch_2, true, false, false)));
	// Change
	ASSERT_EQ (lumex::dev::network_params.work.epoch_2, lumex::dev::network_params.work.threshold (version, lumex::block_details (lumex::epoch::epoch_2, false, false, false)));
	// Receive [+ change] / Open
	ASSERT_EQ (lumex::dev::network_params.work.epoch_2_receive, lumex::dev::network_params.work.threshold (version, lumex::block_details (lumex::epoch::epoch_2, false, true, false)));
	// Epoch
	ASSERT_EQ (lumex::dev::network_params.work.epoch_2_receive, lumex::dev::network_params.work.threshold (version, lumex::block_details (lumex::epoch::epoch_2, false, false, true)));
}
