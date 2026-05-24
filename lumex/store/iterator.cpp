#include <lumex/lib/utility.hpp>
#include <lumex/store/iterator.hpp>
#include <lumex/store/lmdb/iterator.hpp>
#include <lumex/store/rocksdb/iterator.hpp>
#include <lumex/store/transaction.hpp>

namespace lumex::store
{
struct iterator::iterator_wrapper
{
	using iterator_variant = std::variant<lmdb::iterator, rocksdb::iterator>;
	iterator_variant iter;

	iterator_wrapper (lmdb::iterator && i) :
		iter{ std::move (i) }
	{
	}
	iterator_wrapper (rocksdb::iterator && i) :
		iter{ std::move (i) }
	{
	}
};

iterator::iterator (transaction const & txn, lmdb::iterator && internals) :
	txn{ &txn },
	internals{ std::make_unique<iterator_wrapper> (std::move (internals)) },
	transaction_epoch{ txn.epoch () }
{
	update ();
}

iterator::iterator (transaction const & txn, rocksdb::iterator && internals) :
	txn{ &txn },
	internals{ std::make_unique<iterator_wrapper> (std::move (internals)) },
	transaction_epoch{ txn.epoch () }
{
	update ();
}

iterator::~iterator ()
{
	if (txn)
	{
		release_assert (transaction_epoch == txn->epoch (), "invalid iterator-transaction lifetime detected");
	}
}

iterator::iterator (iterator && other) noexcept :
	txn{ other.txn },
	transaction_epoch{ other.transaction_epoch },
	internals{ std::move (other.internals) }
{
	other.txn = nullptr;
	current = std::move (other.current);
}

void iterator::update ()
{
	std::visit ([&] (auto && arg) {
		if (!arg.is_end ())
		{
			this->current = arg.span ();
		}
		else
		{
			current = std::monostate{};
		}
	},
	internals->iter);
}

auto iterator::operator= (iterator && other) noexcept -> iterator &
{
	if (txn)
	{
		release_assert (transaction_epoch == txn->epoch (), "invalid iterator-transaction lifetime detected");
	}
	txn = other.txn;
	transaction_epoch = other.transaction_epoch;
	internals = std::move (other.internals);
	current = std::move (other.current);
	other.txn = nullptr;
	return *this;
}

auto iterator::operator++ () -> iterator &
{
	std::visit ([] (auto && arg) {
		++arg;
	},
	internals->iter);
	update ();
	return *this;
}

auto iterator::operator-- () -> iterator &
{
	std::visit ([] (auto && arg) {
		--arg;
	},
	internals->iter);
	update ();
	return *this;
}

auto iterator::operator->() const -> const_pointer
{
	release_assert (!is_end ());
	return std::get_if<value_type> (&current);
}

auto iterator::operator* () const -> const_reference
{
	release_assert (!is_end ());
	return std::get<value_type> (current);
}

auto iterator::operator== (iterator const & other) const -> bool
{
	return internals->iter == other.internals->iter;
}

bool iterator::is_end () const
{
	return std::holds_alternative<std::monostate> (current);
}

}
