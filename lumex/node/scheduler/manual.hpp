#pragma once

#include <lumex/lib/blocks.hpp>
#include <lumex/lib/locks.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/numbers_templ.hpp>
#include <lumex/node/fwd.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <condition_variable>
#include <future>
#include <memory>
#include <thread>

namespace mi = boost::multi_index;

namespace lumex::scheduler
{
class manual final
{
public:
	explicit manual (lumex::node &);
	~manual ();

	void start ();
	void stop ();

	std::future<std::shared_ptr<lumex::election>> push (std::shared_ptr<lumex::block> const & block);

	bool contains (lumex::block_hash const &) const;

	lumex::container_info container_info () const;

private:
	bool predicate () const;
	void notify ();
	void run ();

private: // Dependencies
	lumex::node & node;

private:
	struct entry
	{
		std::shared_ptr<lumex::block> block;
		mutable std::promise<std::shared_ptr<lumex::election>> promise;

		lumex::block_hash hash () const
		{
			return block->hash ();
		}
	};

	// clang-format off
	class tag_sequenced {};
	class tag_hash {};

	using ordered_queue = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::const_mem_fun<entry, lumex::block_hash, &entry::hash>>
	>>;
	// clang-format on

	ordered_queue queue;

	bool stopped{ false };
	lumex::condition_variable condition;
	mutable lumex::mutex mutex;
	std::thread thread;
};
}
