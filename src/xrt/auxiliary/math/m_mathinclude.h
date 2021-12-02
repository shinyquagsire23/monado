// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Wrapper header for <math.h> to ensure pi-related math constants are
 * defined.
 *
 * Use this instead of directly including <math.H> in headers and when
 * you need M_PI and its friends.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 *
 * @ingroup aux_math
 */

#pragma once

#define _USE_MATH_DEFINES // for M_PI // NOLINT
#ifdef __cplusplus
#include <cmath>
#endif

#include <math.h>


// Might be missing on Windows.
#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

// Might be missing on Windows.
#ifndef M_PIl
#define M_PIl (3.14159265358979323846264338327950288)
#endif

#ifndef M_1_PI
#define M_1_PI (1. / M_PI)
#endif
