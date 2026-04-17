#pragma once

#include <nano/store/fwd.hpp>

#include <cstddef>
#include <iterator>
#include <memory>
#include <span>
#include <utility>
#include <variant>

namespace nano::store::lmdb
{
class iterator;
}

namespace nano::store::rocksdb
{
class iterator;
}

namespace nano::store
{
/**
 * @class iterator
 * @brief A generic database iterator for LMDB or RocksDB.
 *
 * This class represents an iterator for either LMDB or RocksDB (Persistent Key-Value Store) databases.
 * It is a circular iterator, meaning that the end() sentinel value is always in the iteration cycle.
 *
 * Key characteristics:
 * - Decrementing the end iterator points to the last key in the database.
 * - Incrementing the end iterator points to the first key in the database.
 * - Internally uses either an LMDB or RocksDB iterator, abstracted through a std::variant.
 */
class iterator final
{
public:
	using iterator_category = std::bidirectional_iterator_tag;
	using value_type = std::pair<std::span<uint8_t const>, std::span<uint8_t const>>; // <key, value>
	using pointer = value_type *;
	using const_pointer = value_type const *;
	using reference = value_type &;
	using const_reference = value_type const &;

private:
	struct iterator_wrapper;

	nano::store::transaction const * txn;
	std::unique_ptr<iterator_wrapper> internals;
	size_t transaction_epoch;

	std::variant<std::monostate, value_type> current;

public:
	iterator (nano::store::transaction const & txn, lmdb::iterator && internals);
	iterator (nano::store::transaction const & txn, rocksdb::iterator && internals);
	~iterator ();

	iterator (iterator const &) = delete;
	auto operator= (iterator const &) -> iterator & = delete;

	iterator (iterator && other) noexcept;
	auto operator= (iterator && other) noexcept -> iterator &;

	auto operator++ () -> iterator &;
	auto operator-- () -> iterator &;
	auto operator->() const -> const_pointer;
	auto operator* () const -> const_reference;
	auto operator== (iterator const & other) const -> bool;
	bool is_end () const;

private:
	void update ();
};
}
