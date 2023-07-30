// Copyright 2019-2023, Collabora, Ltd.
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


/*
 *
 * Definitions.
 *
 */

#define DEBUG_CHAR_STORAGE_SIZE (1024)

enum debug_tristate_option
{
	DEBUG_TRISTATE_OFF,
	DEBUG_TRISTATE_AUTO,
	DEBUG_TRISTATE_ON
};


/*
 *
 * Conversion functions.
 *
 */

bool
debug_string_to_bool(const char *string);

enum debug_tristate_option
debug_string_to_tristate(const char *string);

long
debug_string_to_num(const char *string, long _default);

float
debug_string_to_float(const char *string, float _default);

enum u_logging_level
debug_string_to_log_level(const char *string, enum u_logging_level _default);


/*
 *
 * Get functions.
 *
 */

const char *
debug_get_option(char *chars, size_t char_count, const char *name, const char *_default);

bool
debug_get_bool_option(const char *name, bool _default);

enum debug_tristate_option
debug_get_tristate_option(const char *name);

long
debug_string_to_num(const char *string, long _default);

long
debug_get_num_option(const char *name, long _default);

float
debug_get_float_option(const char *name, float _default);

enum u_logging_level
debug_get_log_option(const char *name, enum u_logging_level _default);


/*
 *
 * Get once helpers.
 *
 */

#define DEBUG_GET_ONCE_OPTION(suffix, name, _default)                                                                  \
	static const char *debug_get_option_##suffix(void)                                                             \
	{                                                                                                              \
		static char storage[DEBUG_CHAR_STORAGE_SIZE];                                                          \
		static bool gotten = false;                                                                            \
		static const char *stored;                                                                             \
		if (!gotten) {                                                                                         \
			gotten = true;                                                                                 \
			stored = debug_get_option(storage, ARRAY_SIZE(storage), name, _default);                       \
		}                                                                                                      \
		return stored;                                                                                         \
	}

#define DEBUG_GET_ONCE_TRISTATE_OPTION(suffix, name)                                                                   \
	static enum debug_tristate_option debug_get_tristate_option_##suffix(void)                                     \
	{                                                                                                              \
		static bool gotten = false;                                                                            \
		static enum debug_tristate_option stored;                                                              \
		if (!gotten) {                                                                                         \
			gotten = true;                                                                                 \
			stored = debug_get_tristate_option(name);                                                      \
		}                                                                                                      \
		return stored;                                                                                         \
	}

#define DEBUG_GET_ONCE_BOOL_OPTION(suffix, name, _default)                                                             \
	static bool debug_get_bool_option_##suffix(void)                                                               \
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
	static long debug_get_num_option_##suffix(void)                                                                \
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
	static float debug_get_float_option_##suffix(void)                                                             \
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
	static enum u_logging_level debug_get_log_option_##suffix(void)                                                \
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
