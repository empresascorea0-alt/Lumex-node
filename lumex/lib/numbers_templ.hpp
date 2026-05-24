#pragma once

#include <lumex/lib/numbers.hpp>

#include <boost/functional/hash.hpp>

namespace std
{
template <>
struct hash<::lumex::uint128_union>
{
	size_t operator() (::lumex::uint128_union const & value) const noexcept
	{
		return value.qwords[0] + value.qwords[1];
	}
};
template <>
struct hash<::lumex::uint256_union>
{
	size_t operator() (::lumex::uint256_union const & value) const noexcept
	{
		return value.qwords[0] + value.qwords[1] + value.qwords[2] + value.qwords[3];
	}
};
template <>
struct hash<::lumex::public_key>
{
	size_t operator() (::lumex::public_key const & value) const noexcept
	{
		return hash<::lumex::uint256_union>{}(value);
	}
};
template <>
struct hash<::lumex::account>
{
	size_t operator() (::lumex::account const & value) const noexcept
	{
		return hash<::lumex::uint256_union>{}(value);
	}
};
template <>
struct hash<::lumex::block_hash>
{
	size_t operator() (::lumex::block_hash const & value) const noexcept
	{
		return hash<::lumex::uint256_union>{}(value);
	}
};
template <>
struct hash<::lumex::hash_or_account>
{
	size_t operator() (::lumex::hash_or_account const & value) const noexcept
	{
		return hash<::lumex::block_hash>{}(value.as_block_hash ());
	}
};
template <>
struct hash<::lumex::root>
{
	size_t operator() (::lumex::root const & value) const noexcept
	{
		return hash<::lumex::hash_or_account>{}(value);
	}
};
template <>
struct hash<::lumex::link>
{
	size_t operator() (::lumex::link const & value) const noexcept
	{
		return hash<::lumex::hash_or_account>{}(value);
	}
};
template <>
struct hash<::lumex::raw_key>
{
	size_t operator() (::lumex::raw_key const & value) const noexcept
	{
		return hash<::lumex::uint256_union>{}(value);
	}
};
template <>
struct hash<::lumex::wallet_id>
{
	size_t operator() (::lumex::wallet_id const & value) const noexcept
	{
		return hash<::lumex::uint256_union>{}(value);
	}
};
template <>
struct hash<::lumex::uint512_union>
{
	size_t operator() (::lumex::uint512_union const & value) const noexcept
	{
		return hash<::lumex::uint256_union>{}(value.uint256s[0]) + hash<::lumex::uint256_union> () (value.uint256s[1]);
	}
};
template <>
struct hash<::lumex::qualified_root>
{
	size_t operator() (::lumex::qualified_root const & value) const noexcept
	{
		return hash<::lumex::uint512_union>{}(value);
	}
};
template <>
struct hash<::lumex::signature>
{
	size_t operator() (::lumex::signature const & value) const noexcept
	{
		return hash<::lumex::uint512_union>{}(value);
	}
};
}

namespace boost
{
template <>
struct hash<::lumex::uint128_union>
{
	size_t operator() (::lumex::uint128_union const & value) const noexcept
	{
		return std::hash<::lumex::uint128_union> () (value);
	}
};
template <>
struct hash<::lumex::uint256_union>
{
	size_t operator() (::lumex::uint256_union const & value) const noexcept
	{
		return std::hash<::lumex::uint256_union> () (value);
	}
};
template <>
struct hash<::lumex::public_key>
{
	size_t operator() (::lumex::public_key const & value) const noexcept
	{
		return std::hash<::lumex::public_key> () (value);
	}
};
template <>
struct hash<::lumex::account>
{
	size_t operator() (::lumex::account const & value) const noexcept
	{
		return std::hash<::lumex::account> () (value);
	}
};
template <>
struct hash<::lumex::block_hash>
{
	size_t operator() (::lumex::block_hash const & value) const noexcept
	{
		return std::hash<::lumex::block_hash> () (value);
	}
};
template <>
struct hash<::lumex::hash_or_account>
{
	size_t operator() (::lumex::hash_or_account const & value) const noexcept
	{
		return std::hash<::lumex::hash_or_account> () (value);
	}
};
template <>
struct hash<::lumex::root>
{
	size_t operator() (::lumex::root const & value) const noexcept
	{
		return std::hash<::lumex::root> () (value);
	}
};
template <>
struct hash<::lumex::link>
{
	size_t operator() (::lumex::link const & value) const noexcept
	{
		return std::hash<::lumex::link> () (value);
	}
};
template <>
struct hash<::lumex::raw_key>
{
	size_t operator() (::lumex::raw_key const & value) const noexcept
	{
		return std::hash<::lumex::raw_key> () (value);
	}
};
template <>
struct hash<::lumex::wallet_id>
{
	size_t operator() (::lumex::wallet_id const & value) const noexcept
	{
		return std::hash<::lumex::wallet_id> () (value);
	}
};
template <>
struct hash<::lumex::uint512_union>
{
	size_t operator() (::lumex::uint512_union const & value) const noexcept
	{
		return std::hash<::lumex::uint512_union> () (value);
	}
};
template <>
struct hash<::lumex::qualified_root>
{
	size_t operator() (::lumex::qualified_root const & value) const noexcept
	{
		return std::hash<::lumex::qualified_root> () (value);
	}
};
template <>
struct hash<::lumex::signature>
{
	size_t operator() (::lumex::signature const & value) const noexcept
	{
		return std::hash<::lumex::signature> () (value);
	}
};
}
