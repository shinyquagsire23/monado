// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  A minimal way to include Windows.h.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt_config_os.h"

#ifdef XRT_OS_WINDOWS
#ifndef NOBITMAP
#define NOBITMAP
#endif // !NOBITMAP

#ifndef NODRAWTEXT
#define NODRAWTEXT
#endif // !NODRAWTEXT

#ifndef NOGDI
#define NOGDI
#endif // !NOGDI

#ifndef NOHELP
#define NOHELP
#endif // !NOHELP

#ifndef NOMCX
#define NOMCX
#endif // !NOMCX

#ifndef NOMINMAX
#define NOMINMAX
#endif // !NOMINMAX

#ifndef NOSERVICE
#define NOSERVICE
#endif // !NOSERVICE

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN

#include <Windows.h>

#endif // XRT_OS_WINDOWS
