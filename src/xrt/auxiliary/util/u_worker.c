// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Simple worker pool.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 *
 * @ingroup aux_util
 */

#include "os/os_threading.h"

#include "util/u_logging.h"
#include "util/u_worker.h"
#include "util/u_trace_marker.h"


#define MAX_TASK_COUNT (64)
#define MAX_THREAD_COUNT (16)

struct group;
struct pool;

struct task
{
	//! Group this task was submitted from.
	struct group *g;

	//! Function.
	u_worker_group_func_t func;

	//! Function data.
	void *data;
};

struct thread
{
	//! Pool this thread belongs to.
	struct pool *p;

	// Native thread.
	struct os_thread thread;

	//! Thread name.
	char name[64];
};

struct pool
{
	struct u_worker_thread_pool base;

	//! Big contenious mutex.
	struct os_mutex mutex;

	//! Array of tasks.
	struct task tasks[MAX_TASK_COUNT];

	//! Number of tasks in array.
	size_t tasks_in_array_count;

	struct
	{
		size_t count;
		struct os_cond cond;
	} available; //!< For worker threads.

	//! Given at creation.
	uint32_t initial_worker_limit;

	//! Currently the number of works that can work, waiting increases this.
	uint32_t worker_limit;

	//! Number of threads working on tasks.
	size_t working_count;

	//! Number of created threads.
	size_t thread_count;

	//! The worker threads.
	struct thread threads[MAX_THREAD_COUNT];

	//! Is the pool up and running?
	bool running;

	//! Prefix to use for thread names.
	char prefix[32];
};

struct group
{
	//! Base struct has to come first.
	struct u_worker_group base;

	//! Pointer to poll of threads.
	struct u_worker_thread_pool *uwtp;

	//! Number of tasks that is pending or being worked on in this group.
	size_t current_submitted_tasks_count;

	//! Number of threads that have been released or newly entered wait.
	size_t released_count;

	struct
	{
		size_t count;
		struct os_cond cond;
	} waiting; //!< For wait_all
};


/*
 *
 * Helper functions.
 *
 */

static inline struct group *
group(struct u_worker_group *uwg)
{
	return (struct group *)uwg;
}

static inline struct pool *
pool(struct u_worker_thread_pool *uwtp)
{
	return (struct pool *)uwtp;
}


/*
 *
 * Internal pool functions.
 *
 */

static void
locked_pool_pop_task(struct pool *p, struct task *out_task)
{
	assert(p->tasks_in_array_count > 0);

	for (size_t i = 0; i < MAX_TASK_COUNT; i++) {
		if (p->tasks[i].func == NULL) {
			continue;
		}

		*out_task = p->tasks[i];
		p->tasks[i] = (struct task){NULL, NULL, NULL};
		p->tasks_in_array_count--;
		return;
	}

	assert(false);
}

static void
locked_pool_push_task(struct pool *p, struct group *g, u_worker_group_func_t func, void *data)
{
	assert(p->tasks_in_array_count < MAX_TASK_COUNT);

	for (size_t i = 0; i < MAX_TASK_COUNT; i++) {
		if (p->tasks[i].func != NULL) {
			continue;
		}

		p->tasks[i] = (struct task){g, func, data};
		p->tasks_in_array_count++;
		g->current_submitted_tasks_count++;
		return;
	}

	assert(false);
}

static void
locked_pool_wake_worker_if_allowed(struct pool *p)
{
	// No tasks in array, don't wake any thread.
	if (p->tasks_in_array_count == 0) {
		return;
	}

	// The number of working threads is at the limit.
	if (p->working_count >= p->worker_limit) {
		return;
	}

	// No waiting thread.
	if (p->available.count == 0) {
		//! @todo Is this a error?
		return;
	}

	os_cond_signal(&p->available.cond);
}


/*
 *
 * Thread group functions.
 *
 */

static bool
locked_group_should_enter_wait_loop(struct pool *p, struct group *g)
{
	if (g->current_submitted_tasks_count == 0) {
		return false;
	}

	// Enter the loop as a released thread.
	g->released_count++;

	return true;
}

static bool
locked_group_should_wait(struct pool *p, struct group *g)
{
	/*
	 * There are several cases that needs to be covered by this function.
	 *
	 * A thread is entering the wait_all function for the first time, and
	 * work is outstanding what we should do then is increase the worker
	 * limit and wait on the conditional.
	 *
	 * Similar to preceding, we were woken up, there are more work outstanding
	 * on the group and we had been released, remove one released and up the
	 * worker limit, then wait on the conditional.
	 *
	 * A thread (or more) has been woken up and no new tasks has been
	 * submitted, then break out of the loop and decrement the released
	 * count.
	 *
	 * As preceding, but we were one of many woken up but only one thread had
	 * been released and that released count had been taken, then we should
	 * do nothing and wait again.
	 */

	// Tasks available.
	if (g->current_submitted_tasks_count > 0) {

		// We have been released or newly entered the loop.
		if (g->released_count > 0) {
			g->released_count--;
			p->worker_limit++;

			// Wake a worker with the new worker limit.
			locked_pool_wake_worker_if_allowed(p);
		}

		return true;
	}

	// No tasks, and we have been released, party!
	if (g->released_count > 0) {
		g->released_count--;
		return false;
	}

	// We where woken up, but nothing had been released, loop again.
	return true;
}

static void
locked_group_wake_waiter_if_allowed(struct pool *p, struct group *g)
{
	// Are there still outstanding tasks?
	if (g->current_submitted_tasks_count > 0) {
		return;
	}

	// Is there a thread waiting or not?
	if (g->waiting.count == 0) {
		return;
	}

	// Wake one waiting thread.
	os_cond_signal(&g->waiting.cond);

	assert(p->worker_limit > p->initial_worker_limit);

	// Remove one waiting threads.
	p->worker_limit--;

	// We have released one thread.
	g->released_count++;
}

static void
locked_group_wait(struct pool *p, struct group *g)
{
	// Update tracking.
	g->waiting.count++;

	// The wait, also unlocks the mutex.
	os_cond_wait(&g->waiting.cond, &p->mutex);

	// Update tracking.
	g->waiting.count--;
}


/*
 *
 * Thread internal functions.
 *
 */

static bool
locked_thread_allowed_to_work(struct pool *p)
{
	// No work for you!
	if (p->tasks_in_array_count == 0) {
		return false;
	}

	// Reached the limit.
	if (p->working_count >= p->worker_limit) {
		return false;
	}

	return true;
}

static void
locked_thread_wait_for_work(struct pool *p)
{
	// Update tracking.
	p->available.count++;

	// The wait, also unlocks the mutex.
	os_cond_wait(&p->available.cond, &p->mutex);

	// Update tracking.
	p->available.count--;
}

static void *
run_func(void *ptr)
{
	struct thread *t = (struct thread *)ptr;
	struct pool *p = t->p;

	snprintf(t->name, sizeof(t->name), "%s: Worker", p->prefix);
	U_TRACE_SET_THREAD_NAME(t->name);

	os_mutex_lock(&p->mutex);

	while (p->running) {

		if (!locked_thread_allowed_to_work(p)) {
			locked_thread_wait_for_work(p);

			// Check running first when woken up.
			continue;
		}

		// Pop a task from the pool.
		struct task task = {NULL, NULL, NULL};
		locked_pool_pop_task(p, &task);

		// We are now counting as working, needed for wake below.
		p->working_count++;

		// Signal another thread if conditions are met.
		locked_pool_wake_worker_if_allowed(p);

		// Do the actual work here.
		os_mutex_unlock(&p->mutex);
		task.func(task.data);
		os_mutex_lock(&p->mutex);

		// No longer working.
		p->working_count--;

		// Only now decrement the task count on the owning group.
		task.g->current_submitted_tasks_count--;

		// Wake up any waiter.
		locked_group_wake_waiter_if_allowed(p, task.g);
	}

	// Make sure all threads are woken up.
	os_cond_signal(&p->available.cond);

	os_mutex_unlock(&p->mutex);

	return NULL;
}


/*
 *
 * 'Exported' thread pool functions.
 *
 */

struct u_worker_thread_pool *
u_worker_thread_pool_create(uint32_t starting_worker_count, uint32_t thread_count, const char *prefix)
{
	XRT_TRACE_MARKER();
	int ret;

	assert(starting_worker_count < thread_count);
	if (starting_worker_count >= thread_count) {
		return NULL;
	}

	assert(thread_count <= MAX_THREAD_COUNT);
	if (thread_count > MAX_THREAD_COUNT) {
		return NULL;
	}

	struct pool *p = U_TYPED_CALLOC(struct pool);
	p->base.reference.count = 1;
	p->initial_worker_limit = starting_worker_count;
	p->worker_limit = starting_worker_count;
	p->thread_count = thread_count;
	p->running = true;
	snprintf(p->prefix, sizeof(p->prefix), "%s", prefix);

	ret = os_mutex_init(&p->mutex);
	if (ret != 0) {
		goto err_alloc;
	}

	ret = os_cond_init(&p->available.cond);
	if (ret != 0) {
		goto err_mutex;
	}

	for (size_t i = 0; i < thread_count; i++) {
		p->threads[i].p = p;
		os_thread_init(&p->threads[i].thread);
		os_thread_start(&p->threads[i].thread, run_func, &p->threads[i]);
	}

	return (struct u_worker_thread_pool *)p;


err_mutex:
	os_mutex_destroy(&p->mutex);

err_alloc:
	free(p);

	return NULL;
}

void
u_worker_thread_pool_destroy(struct u_worker_thread_pool *uwtp)
{
	XRT_TRACE_MARKER();

	struct pool *p = pool(uwtp);

	os_mutex_lock(&p->mutex);

	p->running = false;
	os_cond_signal(&p->available.cond);
	os_mutex_unlock(&p->mutex);

	// Wait for all threads.
	for (size_t i = 0; i < p->thread_count; i++) {
		os_thread_join(&p->threads[i].thread);
		os_thread_destroy(&p->threads[i].thread);
	}

	os_mutex_destroy(&p->mutex);
	os_cond_destroy(&p->available.cond);

	free(p);
}


/*
 *
 * 'Exported' group functions.
 *
 */

struct u_worker_group *
u_worker_group_create(struct u_worker_thread_pool *uwtp)
{
	XRT_TRACE_MARKER();

	struct group *g = U_TYPED_CALLOC(struct group);
	g->base.reference.count = 1;
	u_worker_thread_pool_reference(&g->uwtp, uwtp);

	os_cond_init(&g->waiting.cond);

	return (struct u_worker_group *)g;
}

void
u_worker_group_push(struct u_worker_group *uwg, u_worker_group_func_t f, void *data)
{
	XRT_TRACE_MARKER();

	struct group *g = group(uwg);
	struct pool *p = pool(g->uwtp);

	os_mutex_lock(&p->mutex);
	while (p->tasks_in_array_count >= MAX_TASK_COUNT) {
		os_mutex_unlock(&p->mutex);

		//! @todo Don't wait all, wait one.
		u_worker_group_wait_all(uwg);

		os_mutex_lock(&p->mutex);
	}

	locked_pool_push_task(p, g, f, data);

	// There are worker threads available, wake one up.
	if (p->available.count > 0) {
		os_cond_signal(&p->available.cond);
	}

	os_mutex_unlock(&p->mutex);
}

void
u_worker_group_wait_all(struct u_worker_group *uwg)
{
	XRT_TRACE_MARKER();

	struct group *g = group(uwg);
	struct pool *p = pool(g->uwtp);

	os_mutex_lock(&p->mutex);

	// Can we early out?
	if (!locked_group_should_enter_wait_loop(p, g)) {
		os_mutex_unlock(&p->mutex);
		return;
	}

	// Wait here until all work been started and completed.
	while (locked_group_should_wait(p, g)) {
		// Do the wait.
		locked_group_wait(p, g);
	}

	os_mutex_unlock(&p->mutex);
}

void
u_worker_group_destroy(struct u_worker_group *uwg)
{
	XRT_TRACE_MARKER();

	struct group *g = group(uwg);
	assert(g->base.reference.count == 0);

	u_worker_group_wait_all(uwg);

	u_worker_thread_pool_reference(&g->uwtp, NULL);

	os_cond_destroy(&g->waiting.cond);

	free(uwg);
}
