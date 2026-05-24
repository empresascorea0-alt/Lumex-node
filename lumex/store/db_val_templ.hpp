#pragma once

#include <lumex/lib/blocks.hpp>
#include <lumex/lib/memory.hpp>
#include <lumex/lib/stream.hpp>
#include <lumex/lib/vote.hpp>
#include <lumex/secure/account_info.hpp>
#include <lumex/secure/pending_info.hpp>
#include <lumex/store/db_val.hpp>

namespace lumex::store
{
// Constructor implementations

inline db_val::db_val (std::shared_ptr<std::vector<uint8_t>> buffer) noexcept :
	buffer{ buffer }
{
	convert_buffer_to_value ();
}

inline db_val::db_val (uint64_t value) :
	buffer{ std::make_shared<std::vector<uint8_t>> () }
{
	{
		boost::endian::native_to_big_inplace (value);
		lumex::vectorstream stream{ *buffer };
		lumex::write (stream, value);
	}
	convert_buffer_to_value ();
}

inline db_val::db_val (lumex::uint128_union const & value) :
	span_view{ value.bytes.data (), sizeof (value) }
{
}

inline db_val::db_val (lumex::uint256_union const & value) :
	span_view{ value.bytes.data (), sizeof (value) }
{
}

inline db_val::db_val (lumex::uint512_union const & value) :
	span_view{ value.bytes.data (), sizeof (value) }
{
}

inline db_val::db_val (lumex::qualified_root const & value) :
	span_view{ reinterpret_cast<uint8_t const *> (&value), sizeof (value) }
{
}

inline db_val::db_val (lumex::account_info const & value) :
	span_view{ reinterpret_cast<uint8_t const *> (&value), value.db_size () }
{
}

inline db_val::db_val (lumex::account_info_v22 const & value) :
	span_view{ reinterpret_cast<uint8_t const *> (&value), value.db_size () }
{
}

inline db_val::db_val (lumex::pending_info const & value) :
	span_view{ reinterpret_cast<uint8_t const *> (&value), value.db_size () }
{
	static_assert (std::is_standard_layout<lumex::pending_info>::value, "Standard layout is required");
}

inline db_val::db_val (lumex::pending_key const & value) :
	span_view{ reinterpret_cast<uint8_t const *> (&value), sizeof (value) }
{
	static_assert (std::is_standard_layout<lumex::pending_key>::value, "Standard layout is required");
}

inline db_val::db_val (lumex::topo_key const & value) :
	buffer{ std::make_shared<std::vector<uint8_t>> () }
{
	{
		lumex::vectorstream stream{ *buffer };
		value.serialize (stream);
	}
	convert_buffer_to_value ();
}

inline db_val::db_val (lumex::confirmation_height_info const & value) :
	buffer{ std::make_shared<std::vector<uint8_t>> () }
{
	{
		lumex::vectorstream stream{ *buffer };
		value.serialize (stream);
	}
	convert_buffer_to_value ();
}

inline db_val::db_val (lumex::block_info const & value) :
	span_view{ reinterpret_cast<uint8_t const *> (&value), sizeof (value) }
{
	static_assert (std::is_standard_layout<lumex::block_info>::value, "Standard layout is required");
}

inline db_val::db_val (lumex::endpoint_key const & value) :
	span_view{ reinterpret_cast<uint8_t const *> (&value), sizeof (value) }
{
	static_assert (std::is_standard_layout<lumex::endpoint_key>::value, "Standard layout is required");
}

inline db_val::db_val (std::shared_ptr<lumex::block> const & block) :
	buffer{ std::make_shared<std::vector<uint8_t>> () }
{
	{
		lumex::vectorstream stream{ *buffer };
		lumex::serialize_block (stream, *block);
	}
	convert_buffer_to_value ();
}

// Conversion operator implementations

inline db_val::operator uint64_t () const
{
	uint64_t result;
	lumex::bufferstream stream{ span_view.data (), span_view.size () };
	auto error{ lumex::try_read (stream, result) };
	(void)error;
	debug_assert (!error);
	boost::endian::big_to_native_inplace (result);
	return result;
}

inline db_val::operator lumex::uint128_union () const
{
	return read_as_bytes<lumex::uint128_union> ();
}

inline db_val::operator lumex::uint256_union () const
{
	return read_as_bytes<lumex::uint256_union> ();
}

inline db_val::operator lumex::uint512_union () const
{
	return read_as_bytes<lumex::uint512_union> ();
}

inline db_val::operator lumex::qualified_root () const
{
	return read_as_bytes<lumex::qualified_root> ();
}

inline db_val::operator lumex::account_info () const
{
	lumex::account_info result;
	debug_assert (span_view.size () == result.db_size ());
	std::copy (span_view.begin (), span_view.end (), reinterpret_cast<uint8_t *> (&result));
	return result;
}

inline db_val::operator lumex::account_info_v22 () const
{
	lumex::account_info_v22 result;
	debug_assert (span_view.size () == result.db_size ());
	std::copy (span_view.begin (), span_view.end (), reinterpret_cast<uint8_t *> (&result));
	return result;
}

inline db_val::operator lumex::pending_info () const
{
	lumex::pending_info result;
	debug_assert (span_view.size () == result.db_size ());
	std::copy (span_view.begin (), span_view.end (), reinterpret_cast<uint8_t *> (&result));
	return result;
}

inline db_val::operator lumex::pending_key () const
{
	lumex::pending_key result;
	debug_assert (span_view.size () == sizeof (result));
	static_assert (sizeof (lumex::pending_key::account) + sizeof (lumex::pending_key::hash) == sizeof (result), "Packed class");
	std::copy (span_view.begin (), span_view.end (), reinterpret_cast<uint8_t *> (&result));
	return result;
}

inline db_val::operator lumex::topo_key () const
{
	lumex::bufferstream stream{ span_view.data (), span_view.size () };
	lumex::topo_key result;
	bool error{ result.deserialize (stream) };
	(void)error;
	debug_assert (!error);
	return result;
}

inline db_val::operator lumex::confirmation_height_info () const
{
	lumex::bufferstream stream{ span_view.data (), span_view.size () };
	lumex::confirmation_height_info result;
	bool error{ result.deserialize (stream) };
	(void)error;
	debug_assert (!error);
	return result;
}

inline db_val::operator block_info () const
{
	lumex::block_info result;
	debug_assert (size () == sizeof (result));
	static_assert (sizeof (lumex::block_info::account) + sizeof (lumex::block_info::balance) == sizeof (result), "Packed class");
	std::copy (span_view.begin (), span_view.end (), reinterpret_cast<uint8_t *> (&result));
	return result;
}

inline db_val::operator lumex::endpoint_key () const
{
	lumex::endpoint_key result;
	debug_assert (span_view.size () == sizeof (result));
	std::copy (span_view.begin (), span_view.end (), reinterpret_cast<uint8_t *> (&result));
	return result;
}

inline db_val::operator std::shared_ptr<lumex::block> () const
{
	lumex::bufferstream stream{ span_view.data (), span_view.size () };
	std::shared_ptr<lumex::block> result{ lumex::deserialize_block (stream) };
	return result;
}

inline db_val::operator lumex::amount () const
{
	return read_as_bytes<lumex::amount> ();
}

inline db_val::operator lumex::block_hash () const
{
	return read_as_bytes<lumex::block_hash> ();
}

inline db_val::operator lumex::public_key () const
{
	return read_as_bytes<lumex::public_key> ();
}

inline db_val::operator lumex::account () const
{
	return read_as_bytes<lumex::account> ();
}

inline db_val::operator std::array<char, 64> () const
{
	lumex::bufferstream stream{ span_view.data (), span_view.size () };
	std::array<char, 64> result;
	auto error = lumex::try_read (stream, result);
	(void)error;
	debug_assert (!error);
	return result;
}

inline db_val::operator lumex::store::block_w_sideband () const
{
	lumex::bufferstream stream{ span_view.data (), span_view.size () };
	lumex::store::block_w_sideband block_w_sideband;
	block_w_sideband.block = (lumex::deserialize_block (stream));
	auto error = block_w_sideband.sideband.deserialize (stream, block_w_sideband.block->type ());
	release_assert (!error);
	block_w_sideband.block->sideband_set (block_w_sideband.sideband);
	return block_w_sideband;
}

inline db_val::operator lumex::store::block_w_sideband_v25 () const
{
	lumex::bufferstream stream{ span_view.data (), span_view.size () };
	lumex::store::block_w_sideband_v25 block_w_sideband;
	block_w_sideband.block = (lumex::deserialize_block (stream));
	auto error = block_w_sideband.sideband.deserialize (stream, block_w_sideband.block->type ());
	release_assert (!error);
	return block_w_sideband;
}

inline db_val::operator std::shared_ptr<lumex::vote> () const
{
	lumex::bufferstream stream{ span_view.data (), span_view.size () };
	auto error{ false };
	auto result{ lumex::make_shared<lumex::vote> (error, stream) };
	debug_assert (!error);
	return result;
}

inline db_val::operator std::nullptr_t () const
{
	return nullptr;
}

inline db_val::operator lumex::no_value () const
{
	return no_value::dummy;
}

template <typename Block>
inline auto db_val::convert_to_block () const -> std::shared_ptr<Block>
{
	lumex::bufferstream stream{ span_view.data (), span_view.size () };
	auto error{ false };
	auto result{ lumex::make_shared<Block> (error, stream) };
	debug_assert (!error);
	return result;
}

inline db_val::operator std::shared_ptr<lumex::send_block> () const
{
	return convert_to_block<lumex::send_block> ();
}

inline db_val::operator std::shared_ptr<lumex::receive_block> () const
{
	return convert_to_block<lumex::receive_block> ();
}

inline db_val::operator std::shared_ptr<lumex::open_block> () const
{
	return convert_to_block<lumex::open_block> ();
}

inline db_val::operator std::shared_ptr<lumex::change_block> () const
{
	return convert_to_block<lumex::change_block> ();
}

inline db_val::operator std::shared_ptr<lumex::state_block> () const
{
	return convert_to_block<lumex::state_block> ();
}

// Member function implementations

inline auto db_val::convert_buffer_to_value () noexcept -> void
{
	if (buffer)
	{
		span_view = std::span<uint8_t const>{ buffer->data (), buffer->size () };
	}
}

template <typename T>
inline auto db_val::read_as_bytes () const -> T
{
	T result;
	debug_assert (span_view.size () == sizeof (result));
	std::copy (span_view.begin (), span_view.end (), result.bytes.data ());
	return result;
}
}