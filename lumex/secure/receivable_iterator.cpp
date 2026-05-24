#include <lumex/secure/ledger_set_any.hpp>
#include <lumex/secure/ledger_set_cemented.hpp>
#include <lumex/secure/receivable_iterator_impl.hpp>

#include <boost/multiprecision/cpp_int.hpp>

template class lumex::receivable_iterator<lumex::ledger_set_any>;
template class lumex::receivable_iterator<lumex::ledger_set_cemented>;
