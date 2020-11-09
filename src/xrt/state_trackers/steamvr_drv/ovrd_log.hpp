// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Logger code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup st_ovrd
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

#include "xrt/xrt_compiler.h"

#ifdef __cplusplus
}
#endif

#include "openvr_driver.h"

static vr::IVRDriverLog *s_pLogFile = NULL;

static inline void
ovrd_log_init(vr::IVRDriverLog *pDriverLog)
{
	// Noop
	s_pLogFile = vr::VRDriverLog();
}

// Can not use the XRT_PRINTF_FORMAT macro on a function definition.
static inline void
ovrd_log(const char *fmt, ...) XRT_PRINTF_FORMAT(1, 2);

static inline void
ovrd_log(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	char buf[1024];
#if defined(WIN32)
	vsprintf_s(buf, fmt, args);
#else
	vsnprintf(buf, sizeof(buf), fmt, args);
#endif

	if (s_pLogFile)
		s_pLogFile->Log(buf);

	va_end(args);
}

static inline void
ovrd_log_cleanup()
{
	s_pLogFile = nullptr;
}
