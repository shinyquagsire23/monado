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

#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) ||          \
    defined(_ARCH_PPC64) || defined(__s390x__) ||                              \
    (defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ == 8)
#define XRT_64_BIT
#else
#define XRT_32_BIT
#endif

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


/*
 * To stop inlining.
 */
#if defined(__GNUC__)
#define XRT_NO_INLINE __attribute__((noinline))
#else
#define XRT_NO_INLINE
#endif


#ifdef XRT_DOXYGEN
/*!
 * To trigger a trap/break in the debugger.
 *
 * @ingroup xrt_iface
 */
#define XRT_DEBUGBREAK()
#elif defined(__clang__) || defined(__GNUC__)
#define XRT_DEBUGBREAK() __builtin_trap()
#elif defined(_MSC_VER)
#include <intrin.h>
#define XRT_DEBUGBREAK() __debugbreak()
#else
#error "compiler not supported"
#endif



#if defined(__GNUC__)
#define xrt_atomic_inc_return(v) __sync_add_and_fetch((v), 1)
#define xrt_atomic_dec_return(v) __sync_sub_and_fetch((v), 1)
#define xrt_atomic_cmpxchg(v, old, _new)                                       \
	__sync_val_compare_and_swap((v), (old), (_new))
#else
#error "compiler not supported"
#endif

/*!
 * Get the holder from a pointer to a field.
 *
 * @ingroup xrt_iface
 */
#define container_of(ptr, type, field)                                         \
	(type *)((char *)ptr - offsetof(type, field))
