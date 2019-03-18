// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Auto detect OS and certain features.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once


/*
 *
 * Auto detect OS.
 *
 */

#if defined(__linux__)
#define XRT_OS_LINUX
#define XRT_OS_UNIX
#define XRT_OS_WAS_AUTODETECTED
#endif



#ifndef XRT_OS_WAS_AUTODETECTED
#error "OS type not found during compile"
#endif
#undef XRT_OS_WAS_AUTODETECTED
