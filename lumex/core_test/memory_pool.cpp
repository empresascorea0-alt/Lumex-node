#include <lumex/lib/blocks.hpp>
#include <lumex/lib/memory.hpp>
#include <lumex/lib/vote.hpp>
#include <lumex/node/active_elections.hpp>
#include <lumex/secure/common.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace
{
/** This allocator records the size of all allocations that happen */
template <class T>
class record_allocations_new_delete_allocator
{
public:
	using value_type = T;

	explicit record_allocations_new_delete_allocator (std::vector<size_t> * allocated) :
		allocated (allocated)
	{
	}

	template <typename U>
	record_allocations_new_delete_allocator (const record_allocations_new_delete_allocator<U> & a)
	{
		allocated = a.allocated;
	}

	template <typename U>
	record_allocations_new_delete_allocator & operator= (const record_allocations_new_delete_allocator<U> &) = delete;

	T * allocate (size_t num_to_allocate)
	{
		auto size_allocated = (sizeof (T) * num_to_allocate);
		allocated->push_back (size_allocated);
		return static_cast<T *> (::operator new (size_allocated));
	}

	void deallocate (T * p, size_t num_to_deallocate)
	{
		::operator delete (p);
	}

	std::vector<size_t> * allocated;
};

template <typename T>
size_t get_allocated_size ()
{
	std::vector<size_t> allocated;
	record_allocations_new_delete_allocator<T> alloc (&allocated);
	(void)std::allocate_shared<T, record_allocations_new_delete_allocator<T>> (alloc);
	debug_assert (allocated.size () == 1);
	return allocated.front ();
}
}

TEST (memory_pool, validate_cleanup)
{
	// This might be turned off, e.g on Mac for instance, so don't do this test
	if (!lumex::get_use_memory_pools ())
	{
		return;
	}

	lumex::make_shared<lumex::open_block> ();
	lumex::make_shared<lumex::receive_block> ();
	lumex::make_shared<lumex::send_block> ();
	lumex::make_shared<lumex::change_block> ();
	lumex::make_shared<lumex::state_block> ();
	lumex::make_shared<lumex::vote> ();

	ASSERT_TRUE (lumex::purge_shared_ptr_singleton_pool_memory<lumex::open_block> ());
	ASSERT_TRUE (lumex::purge_shared_ptr_singleton_pool_memory<lumex::receive_block> ());
	ASSERT_TRUE (lumex::purge_shared_ptr_singleton_pool_memory<lumex::send_block> ());
	ASSERT_TRUE (lumex::purge_shared_ptr_singleton_pool_memory<lumex::state_block> ());
	ASSERT_TRUE (lumex::purge_shared_ptr_singleton_pool_memory<lumex::vote> ());

	// Change blocks have the same size as open_block so won't deallocate any memory
	ASSERT_FALSE (lumex::purge_shared_ptr_singleton_pool_memory<lumex::change_block> ());

	ASSERT_EQ (lumex::determine_shared_ptr_pool_size<lumex::open_block> (), get_allocated_size<lumex::open_block> () - sizeof (size_t));
	ASSERT_EQ (lumex::determine_shared_ptr_pool_size<lumex::receive_block> (), get_allocated_size<lumex::receive_block> () - sizeof (size_t));
	ASSERT_EQ (lumex::determine_shared_ptr_pool_size<lumex::send_block> (), get_allocated_size<lumex::send_block> () - sizeof (size_t));
	ASSERT_EQ (lumex::determine_shared_ptr_pool_size<lumex::change_block> (), get_allocated_size<lumex::change_block> () - sizeof (size_t));
	ASSERT_EQ (lumex::determine_shared_ptr_pool_size<lumex::state_block> (), get_allocated_size<lumex::state_block> () - sizeof (size_t));
	ASSERT_EQ (lumex::determine_shared_ptr_pool_size<lumex::vote> (), get_allocated_size<lumex::vote> () - sizeof (size_t));
}
