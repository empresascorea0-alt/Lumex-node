#include <lumex/lib/networks.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/weights/bootstrap_weights.hpp>
#include <lumex/weights/bootstrap_weights_beta.hpp>
#include <lumex/weights/bootstrap_weights_live.hpp>

#include <boost/multiprecision/cpp_int.hpp>

lumex::bootstrap_weights lumex::get_bootstrap_weights (lumex::network_type type)
{
	std::vector<std::pair<std::string, std::string>> preconfigured;
	uint64_t max_blocks{};

	switch (type)
	{
		case lumex::network_type::lumex_live_network:
			preconfigured = lumex::weights::preconfigured_weights_live;
			max_blocks = lumex::weights::max_blocks_live;
			break;
		case lumex::network_type::lumex_beta_network:
			preconfigured = lumex::weights::preconfigured_weights_beta;
			max_blocks = lumex::weights::max_blocks_beta;
			break;
		case lumex::network_type::lumex_dev_network:
		case lumex::network_type::lumex_test_network:
		case lumex::network_type::invalid:
			return {};
	}

	lumex::bootstrap_weights result{};
	result.max_blocks = max_blocks;
	for (auto const & [account_str, weight_str] : preconfigured)
	{
		lumex::account account;
		bool error = account.decode_account (account_str);
		release_assert (!error, "invalid bootstrap weight account");

		result.representatives[account] = lumex::uint128_t{ weight_str };
	}
	return result;
}
