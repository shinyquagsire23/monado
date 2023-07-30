// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  C++ wrappers for workers.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 *
 * @ingroup aux_util
 */

#pragma once

#include "util/u_worker.h"

#include <vector>
#include <cassert>
#include <functional>


namespace xrt::auxiliary::util {

class TaskCollection;
class SharedThreadGroup;

/*!
 * Wrapper around @ref u_worker_thread_pool.
 *
 * @ingroup aux_util
 */
class SharedThreadPool
{
private:
	u_worker_thread_pool *mPool = nullptr;


public:
	SharedThreadPool(SharedThreadPool const &copy)
	{
		u_worker_thread_pool_reference(&mPool, copy.mPool);
	}

	/*!
	 * Take a C thread pool as argument in case the pool is shared between
	 * different C++ components over C interfaces, or created externally.
	 */
	explicit SharedThreadPool(u_worker_thread_pool *uwtp)
	{
		u_worker_thread_pool_reference(&mPool, uwtp);
	}

	/*!
	 * @copydoc u_worker_thread_pool_create
	 */
	SharedThreadPool(uint32_t starting_worker_count, uint32_t thread_count, const char *prefix)
	{
		mPool = u_worker_thread_pool_create(starting_worker_count, thread_count, prefix);
	}

	~SharedThreadPool()
	{
		u_worker_thread_pool_reference(&mPool, nullptr);
	}

	SharedThreadPool &
	operator=(const SharedThreadPool &other)
	{
		if (this == &other) {
			return *this;
		}

		u_worker_thread_pool_reference(&mPool, other.mPool);
		return *this;
	}

	friend SharedThreadGroup;

	// No default constructor.
	SharedThreadPool() = delete;
	// No move.
	SharedThreadPool(SharedThreadPool &&) = delete;
	// No move assign.
	SharedThreadPool &
	operator=(SharedThreadPool &&) = delete;
};

/*!
 * Wrapper around @ref u_worker_group, use @ref TaskCollection to dispatch work.
 *
 * @ingroup aux_util
 */
class SharedThreadGroup
{
private:
	u_worker_group *mGroup = nullptr;


public:
	SharedThreadGroup(SharedThreadPool const &stp)
	{
		mGroup = u_worker_group_create(stp.mPool);
	}

	~SharedThreadGroup()
	{
		u_worker_group_reference(&mGroup, nullptr);
	}

	friend TaskCollection;

	// No default constructor.
	SharedThreadGroup() = delete;
	// Do not move or copy the shared thread group.
	SharedThreadGroup(SharedThreadGroup const &) = delete;
	SharedThreadGroup(SharedThreadGroup &&) = delete;
	SharedThreadGroup &
	operator=(SharedThreadGroup const &) = delete;
	SharedThreadGroup &
	operator=(SharedThreadGroup &&) = delete;
};

/*!
 * Class to let users fall into a pit of success by
 * being designed as a one shot dispatcher instance.
 *
 * @ingroup aux_util
 */
class TaskCollection
{
public:
	typedef std::function<void()> Functor;


private:
	static constexpr size_t kSize = 16;

	Functor mFunctors[kSize] = {};
	u_worker_group *mGroup = nullptr;


public:
	/*!
	 * Give all Functors when constructed, some what partially
	 * avoids use after leaving scope issues of function delegates.
	 */
	TaskCollection(SharedThreadGroup const &stc, std::vector<Functor> const &funcs)
	{
		assert(funcs.size() <= kSize);

		u_worker_group_reference(&mGroup, stc.mGroup);

		for (size_t i = 0; i < kSize && i < funcs.size(); i++) {
			mFunctors[i] = funcs[i];
			u_worker_group_push(mGroup, &cCallback, &mFunctors[i]);
		}
	}

	~TaskCollection()
	{
		// Also unreferences the group.
		waitAll();
	}

	/*!
	 * Waits for all given tasks to complete, also frees the group.
	 */
	void
	waitAll()
	{
		if (mGroup == nullptr) {
			return;
		}
		u_worker_group_wait_all(mGroup);
		u_worker_group_reference(&mGroup, nullptr);
	}


	// Do not move or copy the task collection.
	TaskCollection(TaskCollection const &) = delete;
	TaskCollection(TaskCollection &&) = delete;
	TaskCollection &
	operator=(TaskCollection const &) = delete;
	TaskCollection &
	operator=(TaskCollection &&) = delete;


private:
	static void
	cCallback(void *data_ptr);
};

} // namespace xrt::auxiliary::util
