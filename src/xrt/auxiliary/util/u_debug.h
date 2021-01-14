// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Small debug helpers.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 *
 * Debug get option helpers heavily inspired from mesa ones.
 */

#pragma once

#include "xrt/xrt_compiler.h"

#include "util/u_logging.h"

#ifdef __cplusplus
extern "C" {
#endif


const char *
debug_get_option(const char *name, const char *_default);

bool
debug_get_bool_option(const char *name, bool _default);

long
debug_get_num_option(const char *name, long _default);

float
debug_get_float_option(const char *name, float _default);

enum u_logging_level
debug_get_log_option(const char *name, enum u_logging_level _default);

#define DEBUG_GET_ONCE_OPTION(suffix, name, _default)                                                                  \
	static const char *debug_get_option_##suffix()                                                                 \
	{                                                                                                              \
		static bool gotten = false;                                                                            \
		static const char *stored;                                                                             \
		if (!gotten) {                                                                                         \
			gotten = true;                                                                                 \
			stored = debug_get_option(name, _default);                                                     \
		}                                                                                                      \
		return stored;                                                                                         \
	}

#define DEBUG_GET_ONCE_BOOL_OPTION(suffix, name, _default)                                                             \
	static bool debug_get_bool_option_##suffix()                                                                   \
	{                                                                                                              \
		static bool gotten = false;                                                                            \
		static bool stored;                                                                                    \
		if (!gotten) {                                                                                         \
			gotten = true;                                                                                 \
			stored = debug_get_bool_option(name, _default);                                                \
		}                                                                                                      \
		return stored;                                                                                         \
	}

#define DEBUG_GET_ONCE_NUM_OPTION(suffix, name, _default)                                                              \
	static long debug_get_num_option_##suffix()                                                                    \
	{                                                                                                              \
		static long gotten = false;                                                                            \
		static long stored;                                                                                    \
		if (!gotten) {                                                                                         \
			gotten = true;                                                                                 \
			stored = debug_get_num_option(name, _default);                                                 \
		}                                                                                                      \
		return stored;                                                                                         \
	}

#define DEBUG_GET_ONCE_FLOAT_OPTION(suffix, name, _default)                                                            \
	static float debug_get_float_option_##suffix()                                                                 \
	{                                                                                                              \
		static long gotten = false;                                                                            \
		static float stored;                                                                                   \
		if (!gotten) {                                                                                         \
			gotten = true;                                                                                 \
			stored = debug_get_float_option(name, _default);                                               \
		}                                                                                                      \
		return stored;                                                                                         \
	}

#define DEBUG_GET_ONCE_LOG_OPTION(suffix, name, _default)                                                              \
	static enum u_logging_level debug_get_log_option_##suffix()                                                    \
	{                                                                                                              \
		static long gotten = false;                                                                            \
		static enum u_logging_level stored;                                                                    \
		if (!gotten) {                                                                                         \
			gotten = true;                                                                                 \
			stored = debug_get_log_option(name, _default);                                                 \
		}                                                                                                      \
		return stored;                                                                                         \
	}

#ifdef __cplusplus
}
#endif
