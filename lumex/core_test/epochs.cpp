#include <lumex/lib/epoch.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/network_params.hpp>

#include <gtest/gtest.h>

TEST (epochs, is_epoch_link)
{
	lumex::epochs epochs;
	// Test epoch 1
	lumex::keypair key1;
	auto link1 = 42;
	auto link2 = 43;
	ASSERT_FALSE (epochs.is_epoch_link (link1));
	ASSERT_FALSE (epochs.is_epoch_link (link2));
	epochs.add (lumex::epoch::epoch_1, key1.pub, link1);
	ASSERT_TRUE (epochs.is_epoch_link (link1));
	ASSERT_FALSE (epochs.is_epoch_link (link2));
	ASSERT_EQ (key1.pub, epochs.signer (lumex::epoch::epoch_1));
	ASSERT_EQ (epochs.epoch (link1), lumex::epoch::epoch_1);

	// Test epoch 2
	lumex::keypair key2;
	epochs.add (lumex::epoch::epoch_2, key2.pub, link2);
	ASSERT_TRUE (epochs.is_epoch_link (link2));
	ASSERT_EQ (key2.pub, epochs.signer (lumex::epoch::epoch_2));
	ASSERT_EQ (lumex::uint256_union (link1), epochs.link (lumex::epoch::epoch_1));
	ASSERT_EQ (lumex::uint256_union (link2), epochs.link (lumex::epoch::epoch_2));
	ASSERT_EQ (epochs.epoch (link2), lumex::epoch::epoch_2);
}

TEST (epochs, is_sequential)
{
	ASSERT_TRUE (lumex::epochs::is_sequential (lumex::epoch::epoch_0, lumex::epoch::epoch_1));
	ASSERT_TRUE (lumex::epochs::is_sequential (lumex::epoch::epoch_1, lumex::epoch::epoch_2));

	ASSERT_FALSE (lumex::epochs::is_sequential (lumex::epoch::epoch_0, lumex::epoch::epoch_2));
	ASSERT_FALSE (lumex::epochs::is_sequential (lumex::epoch::epoch_0, lumex::epoch::invalid));
	ASSERT_FALSE (lumex::epochs::is_sequential (lumex::epoch::unspecified, lumex::epoch::epoch_1));
	ASSERT_FALSE (lumex::epochs::is_sequential (lumex::epoch::epoch_1, lumex::epoch::epoch_0));
	ASSERT_FALSE (lumex::epochs::is_sequential (lumex::epoch::epoch_2, lumex::epoch::epoch_0));
	ASSERT_FALSE (lumex::epochs::is_sequential (lumex::epoch::epoch_2, lumex::epoch::epoch_2));
}
