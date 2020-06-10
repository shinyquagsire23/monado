// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Very simple target main file to export the right symbol.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @ingroup st_ovrd
 */

#include "ovrd_interface.h"

#if defined(_WIN32)
#define HMD_DLL_EXPORT __declspec(dllexport)
#define HMD_DLL_IMPORT __declspec(dllimport)
#elif defined(__GNUC__) || defined(COMPILER_GCC) || defined(__APPLE__)
#define HMD_DLL_EXPORT __attribute__((visibility("default")))
#define HMD_DLL_IMPORT
#else
#error "Unsupported Platform."
#endif


HMD_DLL_EXPORT void *
HmdDriverFactory(const char *pInterfaceName, int *pReturnCode)
{
	return ovrd_hmd_driver_impl(pInterfaceName, pReturnCode);
}
