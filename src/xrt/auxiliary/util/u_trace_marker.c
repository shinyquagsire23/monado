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
#include <inttypes.h>
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


/*
 *
 * Writing functions.
 *
 */

void
u_trace_maker_write_json_metadata(FILE *file, uint32_t pid, uint32_t tid, const char *name)
{
	fprintf(file,
	        ",\n"
	        "\t\t{\n"
	        "\t\t\t\"ph\": \"M\",\n"
	        "\t\t\t\"name\": \"thread_name\",\n"
	        "\t\t\t\"pid\": %u,\n"
	        "\t\t\t\"tid\": %u,\n"
	        "\t\t\t\"args\": {\n"
	        "\t\t\t\t\"name\": \"%s\"\n"
	        "\t\t\t}\n"
	        "\t\t}",
	        pid, tid, name);
}

void
u_trace_maker_write_json_begin(FILE *file,       //
                               uint32_t pid,     //
                               uint32_t tid,     //
                               const char *name, //
                               const char *cat,  //
                               uint64_t when_ns) //
{
	// clang-format off
	fprintf(file,
	        ",\n"
	        "\t\t{\n"
	        "\t\t\t\"ph\": \"B\",\n"
	        "\t\t\t\"name\": \"%s\",\n"
	        "\t\t\t\"cat\": \"%s\",\n"
	        "\t\t\t\"ts\": %" PRIu64 ".%03" PRIu64 ",\n"
	        "\t\t\t\"pid\": %u,\n"
	        "\t\t\t\"tid\": %u,\n"
	        "\t\t\t\"args\": {}\n"
	        "\t\t}",
	        name, cat, when_ns / 1000, when_ns % 1000, pid, tid);
	// clang-format on
}

void
u_trace_maker_write_json_end(FILE *file,       //
                             uint32_t pid,     //
                             uint32_t tid,     //
                             uint64_t when_ns) //
{
	// clang-format off
	fprintf(file,
	        ",\n"
	        "\t\t{\n"
	        "\t\t\t\"ph\": \"E\",\n"
	        "\t\t\t\"ts\": %" PRIu64 ".%03" PRIu64 ",\n"
	        "\t\t\t\"pid\": %u,\n"
	        "\t\t\t\"tid\": %u,\n"
	        "\t\t\t\"args\": {}\n"
	        "\t\t}",
	        when_ns / 1000, when_ns % 1000, pid, tid);
	// clang-format on
}
