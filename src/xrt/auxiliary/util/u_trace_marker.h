// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Tracing support code, see @ref tracing.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_config_os.h"
#include "xrt/xrt_config_have.h"
#include "xrt/xrt_config_build.h"

#include <stdio.h>

#if defined(__cplusplus) && (defined(__clang__) || defined(__GNUC__))
#pragma GCC diagnostic push
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wpedantic"
#elif defined(__clang__)
#pragma GCC diagnostic ignored "-Wc++20-designator"
#endif
#endif

#if defined(XRT_FEATURE_TRACING) && defined(XRT_HAVE_PERCETTO)
#define U_TRACE_PERCETTO
#include <percetto.h>
#endif


#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Should the extra tracks be enabled, see @ref tracing.
 *
 * @ingroup aux_util
 */
enum u_trace_which
{
	U_TRACE_WHICH_SERVICE,
	U_TRACE_WHICH_OPENXR,
};

/*!
 * Internal setup function, use @ref U_TRACE_TARGET_SETUP, see @ref tracing.
 *
 * @ingroup aux_util
 */
void
u_trace_marker_setup(enum u_trace_which which);

/*!
 * Must be called from a non-static/global constructor context.
 *
 * @ingroup aux_util
 */
void
u_trace_marker_init(void);

#define VK_TRACE_MARKER() U_TRACE_FUNC(vk)
#define VK_TRACE_IDENT(IDENT) U_TRACE_IDENT(vk, IDENT)
#define XRT_TRACE_MARKER() U_TRACE_FUNC(xrt)
#define XRT_TRACE_IDENT(IDENT) U_TRACE_IDENT(xrt, IDENT)
#define DRV_TRACE_MARKER() U_TRACE_FUNC(drv)
#define DRV_TRACE_IDENT(IDENT) U_TRACE_IDENT(drv, IDENT)
#define IPC_TRACE_MARKER() U_TRACE_FUNC(ipc)
#define IPC_TRACE_IDENT(IDENT) U_TRACE_IDENT(ipc, IDENT)
#define OXR_TRACE_MARKER() U_TRACE_FUNC(oxr)
#define OXR_TRACE_IDENT(IDENT) U_TRACE_IDENT(oxr, IDENT)
#define COMP_TRACE_MARKER() U_TRACE_FUNC(comp)
#define COMP_TRACE_IDENT(IDENT) U_TRACE_IDENT(comp, IDENT)
#define SINK_TRACE_MARKER() U_TRACE_FUNC(sink)
#define SINK_TRACE_IDENT(IDENT) U_TRACE_IDENT(sink, IDENT)
#define TRACK_TRACE_MARKER() U_TRACE_FUNC(track)
#define TRACK_TRACE_IDENT(IDENT) U_TRACE_IDENT(track, IDENT)


/*
 *
 * When disabled.
 *
 */

#ifndef XRT_FEATURE_TRACING


#define U_TRACE_FUNC(CATEGORY)                                                                                         \
	do {                                                                                                           \
	} while (false)

#define U_TRACE_IDENT(CATEGORY, IDENT)                                                                                 \
	do {                                                                                                           \
	} while (false)

#define U_TRACE_EVENT_BEGIN_ON_TRACK(CATEGORY, TRACK, TIME, NAME)                                                      \
	do {                                                                                                           \
	} while (false)

#define U_TRACE_EVENT_BEGIN_ON_TRACK_DATA(CATEGORY, TRACK, TIME, NAME, ...)                                            \
	do {                                                                                                           \
	} while (false)

#define U_TRACE_EVENT_END_ON_TRACK(CATEGORY, TRACK, TIME)                                                              \
	do {                                                                                                           \
	} while (false)

#define U_TRACE_INSTANT_ON_TRACK(CATEGORY, TRACK, TIME, NAME)                                                          \
	do {                                                                                                           \
	} while (false)

#define U_TRACE_CATEGORY_IS_ENABLED(_) (false)

#define U_TRACE_SET_THREAD_NAME(STRING)                                                                                \
	do {                                                                                                           \
		(void)STRING;                                                                                          \
	} while (false)

/*!
 * Add to target c file to enable tracing, see @ref tracing.
 *
 * @ingroup aux_util
 */
#define U_TRACE_TARGET_SETUP(WHICH)


/*
 *
 * Percetto support.
 *
 */

#elif defined(XRT_HAVE_PERCETTO) // && XRT_FEATURE_TRACKING

#ifndef XRT_OS_LINUX
#error "Tracing only supported on Linux"
#endif


#define U_TRACE_CATEGORIES(C, G)                                                                                       \
	C(vk, "vk")         /* Vulkan calls */                                                                         \
	C(xrt, "xrt")       /* Misc XRT calls */                                                                       \
	C(drv, "drv")       /* Driver calls */                                                                         \
	C(ipc, "ipc")       /* IPC calls */                                                                            \
	C(oxr, "st/oxr")    /* OpenXR State Tracker calls */                                                           \
	C(sink, "sink")     /* Sink/frameserver calls */                                                               \
	C(comp, "comp")     /* Compositor calls  */                                                                    \
	C(track, "track")   /* Tracking calls  */                                                                      \
	C(timing, "timing") /* Timing calls */

PERCETTO_CATEGORY_DECLARE(U_TRACE_CATEGORIES)

PERCETTO_TRACK_DECLARE(pc_cpu);
PERCETTO_TRACK_DECLARE(pc_allotted);
PERCETTO_TRACK_DECLARE(pc_gpu);
PERCETTO_TRACK_DECLARE(pc_margin);
PERCETTO_TRACK_DECLARE(pc_error);
PERCETTO_TRACK_DECLARE(pc_info);
PERCETTO_TRACK_DECLARE(pc_present);
PERCETTO_TRACK_DECLARE(pa_cpu);
PERCETTO_TRACK_DECLARE(pa_draw);
PERCETTO_TRACK_DECLARE(pa_wait);

#define U_TRACE_FUNC(CATEGORY) TRACE_EVENT(CATEGORY, __func__)
#define U_TRACE_IDENT(CATEGORY, IDENT) TRACE_EVENT(CATEGORY, #IDENT)
#define U_TRACE_EVENT_BEGIN_ON_TRACK(CATEGORY, TRACK, TIME, NAME)                                                      \
	TRACE_EVENT_BEGIN_ON_TRACK(CATEGORY, TRACK, TIME, NAME)
#define U_TRACE_EVENT_BEGIN_ON_TRACK_DATA(CATEGORY, TRACK, TIME, NAME, ...)                                            \
	TRACE_EVENT_BEGIN_ON_TRACK_DATA(CATEGORY, TRACK, TIME, NAME, __VA_ARGS__)
#define U_TRACE_EVENT_END_ON_TRACK(CATEGORY, TRACK, TIME) TRACE_EVENT_END_ON_TRACK(CATEGORY, TRACK, TIME)
#define U_TRACE_CATEGORY_IS_ENABLED(CATEGORY) PERCETTO_CATEGORY_IS_ENABLED(CATEGORY)
#define U_TRACE_INSTANT_ON_TRACK(CATEGORY, TRACK, TIME, NAME)                                                          \
	TRACE_ANY_WITH_ARGS(PERCETTO_EVENT_INSTANT, CATEGORY, &g_percetto_track_##TRACK, TIME, NAME, 0)
#define U_TRACE_DATA(fd, type, data) u_trace_data(fd, type, (void *)&(data), sizeof(data))

#define U_TRACE_SET_THREAD_NAME(STRING)                                                                                \
	do {                                                                                                           \
		(void)STRING;                                                                                          \
	} while (false)

#define U_TRACE_TARGET_SETUP(WHICH)                                                                                    \
	void __attribute__((constructor(101))) u_trace_marker_constructor(void);                                       \
                                                                                                                       \
	void u_trace_marker_constructor(void)                                                                          \
	{                                                                                                              \
		u_trace_marker_setup(WHICH);                                                                           \
	}

#else // !XRT_FEATURE_TRACING && !XRT_HAVE_PERCETTO

#error "Need to have Percetto/Perfetto"

#endif // Error checking


#ifdef __cplusplus
}
#endif

#if defined(__cplusplus) && (defined(__clang__) || defined(__GNUC__))
#pragma GCC diagnostic pop
#endif
