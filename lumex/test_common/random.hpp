#pragma once

#include <lumex/lib/numbers.hpp>
#include <lumex/secure/common.hpp>

#include <memory>

namespace lumex::test
{
/*
 * Random generators
 */
lumex::hash_or_account random_hash_or_account ();
lumex::block_hash random_hash ();
lumex::account random_account ();
lumex::qualified_root random_qualified_root ();
lumex::amount random_amount ();
std::shared_ptr<lumex::block> random_block ();
}