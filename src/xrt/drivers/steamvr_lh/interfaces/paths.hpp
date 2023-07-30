// Copyright 2023, Shawn Wallace
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief OpenVR IVRPaths interface header.
 * @author Shawn Wallace <yungwallace@live.com>
 * @ingroup drv_steamvr_lh
 */

#pragma once

#include "openvr_driver.h"

namespace vr {
inline const char *IVRPaths_Version = "IVRPaths_001";
typedef uint64_t PathHandle_t;
} // namespace vr

/** This interface is missing in the C++ header but present in the C one, and the lighthouse driver requires it. */
class Paths
{
	virtual vr::ETrackedPropertyError
	ReadPathBatch(vr::PropertyContainerHandle_t ulRootHandle,
	              struct PathRead_t *pBatch,
	              uint32_t unBatchEntryCount);
	virtual vr::ETrackedPropertyError
	WritePathBatch(vr::PropertyContainerHandle_t ulRootHandle,
	               struct PathWrite_t *pBatch,
	               uint32_t unBatchEntryCount);
	virtual vr::ETrackedPropertyError
	StringToHandle(vr::PathHandle_t *pHandle, char *pchPath);
	virtual vr::ETrackedPropertyError
	HandleToString(vr::PathHandle_t pHandle, char *pchBuffer, uint32_t unBufferSize, uint32_t *punBufferSizeUsed);
};
