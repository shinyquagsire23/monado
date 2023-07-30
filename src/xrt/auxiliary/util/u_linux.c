// Copyright 2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Various helpers for doing Linux specific things.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 *
 * @ingroup aux_util
 */

#include "util/u_linux.h"
#include "util/u_pretty_print.h"

#include <pthread.h>
#include <assert.h>
#include <unistd.h>
#include <sched.h>
#include <stdio.h>

#define LOG_D(...) U_LOG_IFL_D(log_level, __VA_ARGS__)
#define LOG_I(...) U_LOG_IFL_I(log_level, __VA_ARGS__)
#define LOG_W(...) U_LOG_IFL_W(log_level, __VA_ARGS__)
#define LOG_E(...) U_LOG_IFL_E(log_level, __VA_ARGS__)

#define NAME_LENGTH 32


/*
 *
 * Helper functions.
 *
 */

static const char *
policy_to_string(int policy)
{
	switch (policy) {
	case SCHED_FIFO: return "SCHED_FIFO";
	case SCHED_RR: return "SCHED_RR";
	case SCHED_OTHER: return "SCHED_OTHER(normal)";
	case SCHED_IDLE: return "SCHED_IDLE";
	case SCHED_BATCH: return "SCHED_BATCH";
	default: return "SCHED_<UNKNOWN>";
	}
}

static void
get_name(char *str, size_t count)
{
	assert(str != NULL);
	assert(count > 0);

	// First init.
	str[0] = '\0';

	// Get name of thread.
	pthread_t this_thread = pthread_self();
	pthread_getname_np(this_thread, str, count);

	if (str[0] == '\0') {
		snprintf(str, count, "tid(%i)", gettid());
	}
}

static void
print_thread_info(struct u_pp_delegate dg, enum u_logging_level log_level, pthread_t thread)
{
	struct sched_param params;
	int policy = 0;
	int ret = 0;

	// Get the policy and scheduling priority.
	ret = pthread_getschedparam(thread, &policy, &params);
	if (ret != 0) {
		LOG_E("pthread_getschedparam: %i", ret);
		return;
	}

	u_pp(dg, "policy: '%s', priority: '%i'", policy_to_string(policy), params.sched_priority);
}


/*
 *
 * 'Exported' functions.
 *
 */

void
u_linux_try_to_set_realtime_priority_on_thread(enum u_logging_level log_level, const char *name)
{
	pthread_t this_thread = pthread_self();
	struct u_pp_sink_stack_only sink;
	struct sched_param params;
	char str[NAME_LENGTH];
	int ret;

	// Add printing delegate.
	struct u_pp_delegate dg = u_pp_sink_stack_only_init(&sink);

	// Always have some name.
	if (name == NULL) {
		get_name(str, ARRAY_SIZE(str));
		name = str;
	}

	if (log_level <= U_LOGGING_DEBUG) {
		u_pp(dg, "Trying to raise priority on thread '%s'\n\t", name);
		u_pp(dg, "before: ");
		print_thread_info(dg, log_level, this_thread);
	}

	// Get the maximum on this platform.
	params.sched_priority = sched_get_priority_max(SCHED_FIFO);

	// Here we try to set the realtime scheduling with the max priority available.
	ret = pthread_setschedparam(this_thread, SCHED_FIFO, &params);

	// Print different amount depending on log level.
	if (log_level <= U_LOGGING_DEBUG) {
		u_pp(dg, "after: ");
		print_thread_info(dg, log_level, this_thread);
		u_pp(dg, "\n\tResult: %i", ret);
	} else {
		if (ret != 0) {
			u_pp(dg, "Could not raise priority for thread '%s'", name);
		} else {
			u_pp(dg, "Raised priority of thread '%s' to ", name);
			print_thread_info(dg, log_level, this_thread);
		}
	}

	// Always print as warning or information.
	if (ret != 0) {
		LOG_W("%s", sink.buffer);
	} else {
		LOG_I("%s", sink.buffer);
	}
}
