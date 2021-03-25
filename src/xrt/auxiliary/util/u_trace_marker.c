// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Trace marking debugging code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */


// This needs to be first.
#define _GNU_SOURCE

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_config_os.h"
#include "u_trace_marker.h"

#ifdef XRT_OS_LINUX
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdarg.h>
#include <pthread.h>
#include <sys/stat.h>


#define TRACE_MARKER_FILENAME "/sys/kernel/tracing/trace_marker"

int u_trace_xrt_fd = -1;
int u_trace_ipc_fd = -1;
int u_trace_oxr_fd = -1;
int u_trace_comp_fd = -1;

void
u_tracer_maker_init(void)
{
	int fd = open(TRACE_MARKER_FILENAME, O_WRONLY);

	u_trace_oxr_fd = fd;
	u_trace_ipc_fd = fd;
	u_trace_xrt_fd = fd;
	u_trace_comp_fd = fd;
}

void
u_trace_enter(int fd, const char *func)
{
	if (fd < 0) {
		return;
	}

	char tmp[512];
	ssize_t len = snprintf(tmp, sizeof(tmp), "B %u %s", getpid(), func);
	if (len > 0) {
		len = write(fd, tmp, len);
	}
}

void
u_trace_leave(int fd, const char *func)
{
	if (fd < 0) {
		return;
	}

	char tmp[512];
	ssize_t len = snprintf(tmp, sizeof(tmp), "E %u %s", getpid(), func);
	if (len > 0) {
		len = write(fd, tmp, len);
	}
}

void
u_trace_data(int fd, enum u_trace_data_type type, void *data, size_t size)
{
	char tmp[1024 * 8];

	ssize_t len = snprintf(tmp, sizeof(tmp), "r %u %u %u ", getpid(), type, (uint32_t)size);
	if (len <= 0) {
		return;
	}

	for (size_t i = 0; i < size; i++) {
		ssize_t ret = snprintf(tmp + (size_t)len, sizeof(tmp) - (size_t)len, "%02x", ((uint8_t *)data)[i]);
		if (ret <= 0) {
			return;
		}
		len += ret;
	}

	if (len > 0) {
		len = write(fd, tmp, len);
	}
}

#else

// Stubs on non-linux for now.
void
u_tracer_maker_init(void)
{}

void
u_trace_enter(int fd, const char *func)
{}

void
u_trace_leave(int fd, const char *func)
{}

void
u_trace_data(int fd, enum u_trace_data_type type, void *data, size_t size)
{}
#endif
