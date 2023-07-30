
// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Simple process handling
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @ingroup aux_util
 */

#include "xrt/xrt_config.h"
#include "xrt/xrt_config_build.h"

#ifdef XRT_OS_LINUX

#ifdef XRT_HAVE_LIBBSD
#include <bsd/libutil.h>
#endif

#include <limits.h>

#include <errno.h>
#include <stdbool.h>
#include "u_file.h"
#include "u_logging.h"

XRT_MAYBE_UNUSED static inline int
get_pidfile_path(char *buf)
{
	int size = u_file_get_path_in_runtime_dir(XRT_IPC_SERVICE_PID_FILENAME, buf, PATH_MAX);
	if (size == -1) {
		U_LOG_W("Failed to determine runtime dir, not creating pidfile");
		return -1;
	}
	return 0;
}

#endif

#include "u_misc.h"

struct u_process
{
#ifdef XRT_HAVE_LIBBSD
	struct pidfh *pfh;
#else
	int pid;
#endif
};

struct u_process *
u_process_create_if_not_running(void)
{
#ifdef XRT_HAVE_LIBBSD

	char tmp[PATH_MAX];
	if (get_pidfile_path(tmp) < 0) {
		U_LOG_W("Failed to determine runtime dir, not creating pidfile");
		return NULL;
	}

	U_LOG_T("Using pidfile %s", tmp);

	pid_t otherpid;

	struct pidfh *pfh = pidfile_open(tmp, 0600, &otherpid);
	if (errno == EEXIST || pfh == NULL) {
		U_LOG_T("Failed to create pidfile (%s): Another Monado instance may be running", strerror(errno));
		// other process is locking pid file
		return NULL;
	}

	// either new or stale pidfile opened

	int write_ret = pidfile_write(pfh);
	if (write_ret != 0) {
		pidfile_close(pfh);
		return NULL;
	}

	struct u_process *ret = U_TYPED_CALLOC(struct u_process);
	ret->pfh = pfh;

	U_LOG_T("No other Monado instance was running, got new pidfile");
	return ret;
#else
	struct u_process *ret = U_TYPED_CALLOC(struct u_process);
	//! @todo alternative implementation
	ret->pid = 0;
	return ret;
#endif
}

void
u_process_destroy(struct u_process *proc)
{
	if (proc == NULL) {
		return;
	}

#ifdef XRT_HAVE_LIBBSD
	pidfile_close(proc->pfh);
#endif
	free(proc);
}
