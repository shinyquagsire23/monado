// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Trace marking debugging code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_config_os.h"
#include "xrt/xrt_config_build.h"

#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif


enum u_trace_data_type
{
	U_TRACE_DATA_TYPE_TIMING_FRAME,
};

void
u_tracer_maker_init(void);
void
u_trace_enter(int fd, const char *func);
void
u_trace_leave(int fd, const char *func);
void
u_trace_data(int fd, enum u_trace_data_type type, void *data, size_t size);

extern int u_trace_xrt_fd;
extern int u_trace_ipc_fd;
extern int u_trace_oxr_fd;
extern int u_trace_comp_fd;

#define XRT_TRACE_MARKER() U_TRACE_MARKER(u_trace_xrt_fd)
#define IPC_TRACE_MARKER() U_TRACE_MARKER(u_trace_ipc_fd)
#define OXR_TRACE_MARKER() U_TRACE_MARKER(u_trace_oxr_fd)
#define COMP_TRACE_MARKER() U_TRACE_MARKER(u_trace_comp_fd)
#define COMP_TRACE_DATA(type, data) U_TRACE_DATA(u_trace_comp_fd, type, data)


/*
 *
 * JSON dumper helper files.
 *
 */

void
u_trace_maker_write_json_metadata( //
    FILE *file,                    //
    uint32_t pid,                  //
    uint32_t tid,                  //
    const char *name);             //

void
u_trace_maker_write_json_begin( //
    FILE *file,                 //
    uint32_t pid,               //
    uint32_t tid,               //
    const char *name,           //
    const char *cat,            //
    uint64_t when_ns);          //

void
u_trace_maker_write_json_end( //
    FILE *file,               //
    uint32_t pid,             //
    uint32_t tid,             //
    uint64_t when_ns);        //


/*
 *
 * Functions implemented by other modules.
 *
 */

void
u_ft_write_json(FILE *file, void *data);

void
u_ft_write_json_metadata(FILE *file);


/*
 *
 * When enabled.
 *
 */

#ifdef XRT_FEATURE_TRACING

#ifndef XRT_OS_LINUX
#error "Tracing only supported on Linux"
#endif

struct u_trace_scoped_struct
{
	const char *name;
	int data;
};

static inline void
u_trace_scope_cleanup(struct u_trace_scoped_struct *data)
{
	u_trace_leave(data->data, data->name);
}

#define U_TRACE_MARKER(fd)                                                                                             \
	struct u_trace_scoped_struct __attribute__((cleanup(u_trace_scope_cleanup)))                                   \
	    u_trace_marker_func_data = {__func__, fd};                                                                 \
	u_trace_enter(u_trace_marker_func_data.data, u_trace_marker_func_data.name)



#define U_TRACE_DATA(fd, type, data) u_trace_data(fd, type, (void *)&(data), sizeof(data))


#define U_TRACE_TARGET_INIT()                                                                                          \
	void __attribute__((constructor(101))) u_trace_maker_constructor(void);                                        \
                                                                                                                       \
	void u_trace_maker_constructor(void)                                                                           \
	{                                                                                                              \
		u_tracer_maker_init();                                                                                 \
	}


#else // XRT_FEATURE_TRACING


/*
 *
 * When disabled.
 *
 */

#define U_TRACE_MARKER(fd)                                                                                             \
	do {                                                                                                           \
	} while (false)

#define U_TRACE_DATA(fd, type, data)                                                                                   \
	do {                                                                                                           \
	} while (false)

#define U_TRACE_TARGET_INIT()

#endif // XRT_FEATURE_TRACING


#ifdef __cplusplus
}
#endif
