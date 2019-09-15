// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Wrapper around OS threading native functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 *
 * @ingroup aux_os
 */

#include "xrt/xrt_compiler.h"
#include "util/u_misc.h"

#ifdef XRT_OS_LINUX
#include <pthread.h>
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
XRT_MAYBE_UNUSED static int
os_mutex_init(struct os_mutex *om)
{
	return pthread_mutex_init(&om->mutex, NULL);
}

/*!
 * Lock.
 */
XRT_MAYBE_UNUSED static void
os_mutex_lock(struct os_mutex *om)
{
	pthread_mutex_lock(&om->mutex);
}

/*!
 * Unlock.
 */
XRT_MAYBE_UNUSED static void
os_mutex_unlock(struct os_mutex *om)
{
	pthread_mutex_unlock(&om->mutex);
}

/*!
 * Clean up.
 */
XRT_MAYBE_UNUSED static void
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
XRT_MAYBE_UNUSED static int
os_thread_init(struct os_thread *ost)
{
	return 0;
}

/*!
 * Start thread.
 */
XRT_MAYBE_UNUSED static int
os_thread_start(struct os_thread *ost, os_run_func func, void *ptr)
{
	return pthread_create(&ost->thread, NULL, func, ptr);
}

/*!
 * Join.
 */
XRT_MAYBE_UNUSED static void
os_thread_join(struct os_thread *ost)
{
	void *retval;

	pthread_join(ost->thread, &retval);
	U_ZERO(&ost->thread);
}

/*!
 * Destruction.
 */
XRT_MAYBE_UNUSED static void
os_thread_destroy(struct os_thread *ost)
{}


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
XRT_MAYBE_UNUSED static int
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
XRT_MAYBE_UNUSED static int
os_thread_helper_start(struct os_thread_helper *oth,
                       os_run_func func,
                       void *ptr)
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
XRT_MAYBE_UNUSED static int
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
XRT_MAYBE_UNUSED static void
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
XRT_MAYBE_UNUSED static void
os_thread_helper_lock(struct os_thread_helper *oth)
{
	pthread_mutex_lock(&oth->mutex);
}

/*!
 * Unlock the helper.
 */
XRT_MAYBE_UNUSED static void
os_thread_helper_unlock(struct os_thread_helper *oth)
{
	pthread_mutex_unlock(&oth->mutex);
}

/*!
 * Is the thread running, or suppised to be running.
 *
 * Must be called with the helper locked.
 */
XRT_MAYBE_UNUSED static bool
os_thread_helper_is_running_locked(struct os_thread_helper *oth)
{
	return oth->running;
}

/*!
 * Wait for a signal.
 *
 * Must be called with the helper locked.
 */
XRT_MAYBE_UNUSED static void
os_thread_helper_wait_locked(struct os_thread_helper *oth)
{
	pthread_cond_wait(&oth->cond, &oth->mutex);
}

/*!
 * Signal a waiting thread to wake up.
 *
 * Must be called with the helper locked.
 */
XRT_MAYBE_UNUSED static void
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
