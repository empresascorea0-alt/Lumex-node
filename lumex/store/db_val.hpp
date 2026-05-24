#pragma once

#include <lumex/lib/blocks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/stream.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/endpoint_key.hpp>
#include <lumex/store/block_w_sideband.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace lumex
{
class account_info;
class account_info_v22;
class block;
class pending_info;
class pending_key;
class topo_key;
class vote;
}

namespace lumex::store
{
/**
 * Encapsulates database values using std::span for type safety and backend independence
 */
class db_val final
{
public:
	db_val () = default;

	db_val (std::span<uint8_t const> span) noexcept :
		span_view{ span }
	{
	}

	db_val (size_t size, void const * data) noexcept :
		span_view{ static_cast<uint8_t const *> (data), size }
	{
	}

	db_val (std::nullptr_t) noexcept :
		span_view{}
	{
	}

	// Non-owning view over the characters of `str`; `str` must outlive the db_val
	db_val (std::string_view str) noexcept :
		span_view{ reinterpret_cast<uint8_t const *> (str.data ()), str.size () }
	{
	}

	// Non-owning view over the characters of `str`; `str` must outlive the db_val
	db_val (std::string const & str) noexcept :
		span_view{ reinterpret_cast<uint8_t const *> (str.data ()), str.size () }
	{
	}

	// Non-owning view over the characters of `str`; `str` must outlive the db_val
	db_val (char const * str) noexcept :
		db_val (std::string_view{ str })
	{
	}

	db_val (std::shared_ptr<std::vector<uint8_t>> buffer) noexcept;

	db_val (uint64_t value);
	db_val (lumex::uint128_union const &);
	db_val (lumex::uint256_union const &);
	db_val (lumex::uint512_union const &);
	db_val (lumex::qualified_root const &);
	db_val (lumex::account_info const &);
	db_val (lumex::account_info_v22 const &);
	db_val (lumex::pending_info const &);
	db_val (lumex::pending_key const &);
	db_val (lumex::topo_key const &);
	db_val (lumex::confirmation_height_info const &);
	db_val (lumex::block_info const &);
	db_val (lumex::endpoint_key const &);
	db_val (std::shared_ptr<lumex::block> const &);

	explicit operator uint64_t () const;
	explicit operator lumex::uint128_union () const;
	explicit operator lumex::uint256_union () const;
	explicit operator lumex::uint512_union () const;
	explicit operator lumex::qualified_root () const;
	explicit operator lumex::account_info () const;
	explicit operator lumex::account_info_v22 () const;
	explicit operator lumex::pending_info () const;
	explicit operator lumex::pending_key () const;
	explicit operator lumex::topo_key () const;
	explicit operator lumex::confirmation_height_info () const;
	explicit operator lumex::block_info () const;
	explicit operator lumex::endpoint_key () const;
	explicit operator std::shared_ptr<lumex::block> () const;
	explicit operator lumex::amount () const;
	explicit operator lumex::block_hash () const;
	explicit operator lumex::public_key () const;
	explicit operator lumex::account () const;
	explicit operator std::array<char, 64> () const;
	explicit operator block_w_sideband () const;
	explicit operator block_w_sideband_v25 () const;
	explicit operator std::shared_ptr<lumex::vote> () const;
	explicit operator std::nullptr_t () const;
	explicit operator lumex::no_value () const;

	template <typename Block>
	auto convert_to_block () const -> std::shared_ptr<Block>;

	template <typename T>
	T convert_to () const
	{
		return static_cast<T> (*this);
	}

	explicit operator std::shared_ptr<lumex::send_block> () const;
	explicit operator std::shared_ptr<lumex::receive_block> () const;
	explicit operator std::shared_ptr<lumex::open_block> () const;
	explicit operator std::shared_ptr<lumex::change_block> () const;
	explicit operator std::shared_ptr<lumex::state_block> () const;

	auto data () const noexcept -> void *
	{
		return const_cast<void *> (static_cast<void const *> (span_view.data ()));
	}
	auto size () const noexcept -> size_t
	{
		return span_view.size ();
	}

	auto convert_buffer_to_value () noexcept -> void;

	std::span<uint8_t const> span_view;
	std::shared_ptr<std::vector<uint8_t>> buffer;

private:
	template <typename T>
	auto read_as_bytes () const -> T;
};
}
