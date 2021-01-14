// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Wrapper around OS threading native functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 *
 * @ingroup aux_os
 */

#pragma once

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_config_os.h"

#include "util/u_misc.h"

#include "os/os_time.h"

#if defined(XRT_OS_LINUX)
#include <pthread.h>
#include <semaphore.h>
#include <assert.h>
#elif defined(XRT_OS_WINDOWS)
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <assert.h>
#else
#error "OS not supported"
#endif

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @ingroup aux_os
 * @{
 */

/*
 *
 * Mutex
 *
 */

/*!
 * A wrapper around a native mutex.
 */
struct os_mutex
{
	pthread_mutex_t mutex;
};

/*!
 * Init.
 */
static inline int
os_mutex_init(struct os_mutex *om)
{
	return pthread_mutex_init(&om->mutex, NULL);
}

/*!
 * Lock.
 */
static inline void
os_mutex_lock(struct os_mutex *om)
{
	pthread_mutex_lock(&om->mutex);
}

/*!
 * Unlock.
 */
static inline void
os_mutex_unlock(struct os_mutex *om)
{
	pthread_mutex_unlock(&om->mutex);
}

/*!
 * Clean up.
 */
static inline void
os_mutex_destroy(struct os_mutex *om)
{
	pthread_mutex_destroy(&om->mutex);
}


/*
 *
 * Thread.
 *
 */

/*!
 * A wrapper around a native mutex.
 */
struct os_thread
{
	pthread_t thread;
};

/*!
 * Run function.
 */
typedef void *(*os_run_func)(void *);

/*!
 * Init.
 */
static inline int
os_thread_init(struct os_thread *ost)
{
	return 0;
}

/*!
 * Start thread.
 */
static inline int
os_thread_start(struct os_thread *ost, os_run_func func, void *ptr)
{
	return pthread_create(&ost->thread, NULL, func, ptr);
}

/*!
 * Join.
 */
static inline void
os_thread_join(struct os_thread *ost)
{
	void *retval;

	pthread_join(ost->thread, &retval);
	U_ZERO(&ost->thread);
}

/*!
 * Destruction.
 */
static inline void
os_thread_destroy(struct os_thread *ost)
{}


/*
 *
 * Semaphore.
 *
 */

/*!
 * A wrapper around a native semaphore.
 */
struct os_semaphore
{
	sem_t sem;
};

/*!
 * Init.
 */
static inline int
os_semaphore_init(struct os_semaphore *os, int count)
{
	return sem_init(&os->sem, 0, count);
}

/*!
 * Release.
 */
static inline void
os_semaphore_release(struct os_semaphore *os)
{
	sem_post(&os->sem);
}

/*!
 * Wait, if @p timeout_ns is zero then waits forever.
 */
static inline void
os_semaphore_wait(struct os_semaphore *os, uint64_t timeout_ns)
{
	if (timeout_ns == 0) {
		sem_wait(&os->sem);
		return;
	}

	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
		assert(false);
	}

	uint64_t now_ns = os_timespec_to_ns(&ts);
	uint64_t when_ns = timeout_ns + now_ns;

	struct timespec abs_timeout = {0, 0};
	os_ns_to_timespec(when_ns, &abs_timeout);

	sem_timedwait(&os->sem, &abs_timeout);
}

/*!
 * Clean up.
 */
static inline void
os_semaphore_destroy(struct os_semaphore *os)
{
	sem_destroy(&os->sem);
}


/*
 *
 * Fancy helper.
 *
 */

/*!
 * All in one helper that handles locking, waiting for change and starting a
 * thread.
 */
struct os_thread_helper
{
	pthread_t thread;
	pthread_mutex_t mutex;
	pthread_cond_t cond;

	bool running;
};

/*!
 * Initialize the thread helper.
 */
static inline int
os_thread_helper_init(struct os_thread_helper *oth)
{
	int ret = pthread_mutex_init(&oth->mutex, NULL);
	if (ret != 0) {
		return ret;
	}

	ret = pthread_cond_init(&oth->cond, NULL);
	if (ret) {
		pthread_mutex_destroy(&oth->mutex);
		return ret;
	}

	return 0;
}

/*!
 * Start the internal thread.
 */
static inline int
os_thread_helper_start(struct os_thread_helper *oth, os_run_func func, void *ptr)
{
	pthread_mutex_lock(&oth->mutex);

	if (oth->running) {
		pthread_mutex_unlock(&oth->mutex);
		return -1;
	}

	int ret = pthread_create(&oth->thread, NULL, func, ptr);
	if (ret != 0) {
		pthread_mutex_unlock(&oth->mutex);
		return ret;
	}

	oth->running = true;

	pthread_mutex_unlock(&oth->mutex);

	return 0;
}

/*!
 * Stop the thread.
 */
static inline int
os_thread_helper_stop(struct os_thread_helper *oth)
{
	void *retval = NULL;

	// The fields are protected.
	pthread_mutex_lock(&oth->mutex);

	if (!oth->running) {
		pthread_mutex_unlock(&oth->mutex);
		return 0;
	}

	// Stop the thread.
	oth->running = false;

	// Wake up the thread if it is waiting.
	pthread_cond_signal(&oth->cond);

	// No longer need to protect fields.
	pthread_mutex_unlock(&oth->mutex);

	// Wait for thread to finish.
	pthread_join(oth->thread, &retval);

	return 0;
}

/*!
 * Destroy the thread helper, externally syncronizable.
 */
static inline void
os_thread_helper_destroy(struct os_thread_helper *oth)
{
	// Stop the thread.
	os_thread_helper_stop(oth);

	// Destroy resources.
	pthread_mutex_destroy(&oth->mutex);
	pthread_cond_destroy(&oth->cond);
}

/*!
 * Lock the helper.
 */
static inline void
os_thread_helper_lock(struct os_thread_helper *oth)
{
	pthread_mutex_lock(&oth->mutex);
}

/*!
 * Unlock the helper.
 */
static inline void
os_thread_helper_unlock(struct os_thread_helper *oth)
{
	pthread_mutex_unlock(&oth->mutex);
}

/*!
 * Is the thread running, or supposed to be running.
 *
 * Must be called with the helper locked.
 */
static inline bool
os_thread_helper_is_running_locked(struct os_thread_helper *oth)
{
	return oth->running;
}

/*!
 * Wait for a signal.
 *
 * Must be called with the helper locked.
 */
static inline void
os_thread_helper_wait_locked(struct os_thread_helper *oth)
{
	pthread_cond_wait(&oth->cond, &oth->mutex);
}

/*!
 * Signal a waiting thread to wake up.
 *
 * Must be called with the helper locked.
 */
static inline void
os_thread_helper_signal_locked(struct os_thread_helper *oth)
{
	pthread_cond_signal(&oth->cond);
}


/*!
 * @}
 */


#ifdef __cplusplus
} // extern "C"
#endif
