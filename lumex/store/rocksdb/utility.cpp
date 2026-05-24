#include <lumex/store/rocksdb/transaction_rocksdb.hpp>
#include <lumex/store/rocksdb/utility.hpp>

auto lumex::store::rocksdb::tx (store::transaction const & transaction_a) -> std::variant<::rocksdb::Transaction *, ::rocksdb::ReadOptions *>
{
	if (dynamic_cast<lumex::store::read_transaction const *> (&transaction_a) != nullptr)
	{
		return static_cast<::rocksdb::ReadOptions *> (transaction_a.get_handle ());
	}
	return static_cast<::rocksdb::Transaction *> (transaction_a.get_handle ());
}
