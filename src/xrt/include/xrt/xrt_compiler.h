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

#ifdef _MSC_VER
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif // !NOMINMAX

#include <intrin.h>
// for atomic intrinsics
#include <windows.h>
#endif // _MSC_VER

/*!
 * Array size helper.
 */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_ARCH_PPC64) || defined(__s390x__) ||    \
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
#elif defined(_MSC_VER) && defined(__cplusplus)
#define XRT_MAYBE_UNUSED [[maybe_unused]]
#else
#define XRT_MAYBE_UNUSED
#endif


/*
 * To stop inlining.
 */
#if defined(__GNUC__)
#define XRT_NO_INLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define XRT_NO_INLINE __declspec(noinline)
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
#define XRT_DEBUGBREAK() __debugbreak()
#else
#error "compiler not supported"
#endif



typedef volatile int32_t xrt_atomic_s32_t;

static inline int32_t
xrt_atomic_s32_inc_return(xrt_atomic_s32_t *p)
{
#if defined(__GNUC__)
	return __sync_add_and_fetch(p, 1);
#elif defined(_MSC_VER)
	return InterlockedIncrement((volatile LONG *)p);
#else
#error "compiler not supported"
#endif
}
static inline int32_t
xrt_atomic_s32_dec_return(xrt_atomic_s32_t *p)
{
#if defined(__GNUC__)
	return __sync_sub_and_fetch(p, 1);
#elif defined(_MSC_VER)
	return InterlockedDecrement((volatile LONG *)p);
#else
#error "compiler not supported"
#endif
}
static inline int32_t
xrt_atomic_s32_cmpxchg(xrt_atomic_s32_t *p, int32_t old_, int32_t new_)
{
#if defined(__GNUC__)
	return __sync_val_compare_and_swap(p, old_, new_);
#elif defined(_MSC_VER)
	return InterlockedCompareExchange((volatile LONG *)p, old_, new_);
#else
#error "compiler not supported"
#endif
}

#ifdef _MSC_VER
#ifdef XRT_64_BIT
typedef int64_t ssize_t;
#else
typedef int32_t ssize_t;
#endif
#endif

/*!
 * Get the holder from a pointer to a field.
 *
 * @ingroup xrt_iface
 */
#define container_of(ptr, type, field) (type *)((char *)ptr - offsetof(type, field))
