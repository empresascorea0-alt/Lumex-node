#include <lumex/crypto_lib/random_pool.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/keypair.hpp>
#include <lumex/test_common/random.hpp>

lumex::hash_or_account lumex::test::random_hash_or_account ()
{
	lumex::hash_or_account random_hash;
	lumex::random_pool::generate_block (random_hash.bytes.data (), random_hash.bytes.size ());
	return random_hash;
}

lumex::block_hash lumex::test::random_hash ()
{
	return lumex::test::random_hash_or_account ().as_block_hash ();
}

lumex::account lumex::test::random_account ()
{
	return lumex::test::random_hash_or_account ().as_account ();
}

lumex::qualified_root lumex::test::random_qualified_root ()
{
	return { lumex::test::random_hash (), lumex::test::random_hash () };
}

lumex::amount lumex::test::random_amount ()
{
	lumex::amount result;
	lumex::random_pool::generate_block (result.bytes.data (), result.bytes.size ());
	return result;
}

std::shared_ptr<lumex::block> lumex::test::random_block ()
{
	lumex::keypair key;
	auto block = std::make_shared<lumex::state_block> (
	lumex::test::random_account (),
	lumex::test::random_hash (),
	lumex::test::random_account (),
	lumex::test::random_amount (),
	lumex::test::random_hash (),
	key.prv,
	key.pub,
	0);
	return block;
}