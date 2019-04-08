// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Header holding common defines.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once


/*
 * C99 is not a high bar to reach.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>


/*!
 * Array size helper.
 */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))


/*
 * Printf helper attribute.
 */
#if defined(__GNUC__)
#define XRT_PRINTF_FORMAT(fmt, list) __attribute__((format(printf, fmt, list)))
#else
#define XRT_PRINTF_FORMAT(fmt, list)
#endif


/*
 * To silence unused warnings.
 */
#if defined(__GNUC__)
#define XRT_MAYBE_UNUSED __attribute__((unused))
#else
#define XRT_MAYBE_UNUSED
#endif

/*!
 * @define XRT_DEBUGBREAK()
 * To trigger a trap/break in the debugger.
 */
#if defined(__clang__) || defined(__GNUC__) 
#define XRT_DEBUGBREAK() __builtin_trap()
#elif defined(_MSC_VER)
#include <intrin.h>
#define XRT_DEBUGBREAK() __debugbreak()
#else
#error "compiler not supported"
#endif
