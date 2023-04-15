// Copyright 2023, Shawn Wallace
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief OpenVR IVRPaths interface implementation.
 * @author Shawn Wallace <yungwallace@live.com>
 * @ingroup drv_steamvr_lh
 */

#include "paths.hpp"

vr::ETrackedPropertyError
Paths::ReadPathBatch(vr::PropertyContainerHandle_t ulRootHandle, struct PathRead_t *pBatch, uint32_t unBatchEntryCount)
{
	return vr::TrackedProp_Success;
}

vr::ETrackedPropertyError
Paths::WritePathBatch(vr::PropertyContainerHandle_t ulRootHandle,
                      struct PathWrite_t *pBatch,
                      uint32_t unBatchEntryCount)
{
	return vr::TrackedProp_Success;
}

vr::ETrackedPropertyError
Paths::StringToHandle(vr::PathHandle_t *pHandle, char *pchPath)
{
	return vr::TrackedProp_Success;
}

vr::ETrackedPropertyError
Paths::HandleToString(vr::PathHandle_t pHandle, char *pchBuffer, uint32_t unBufferSize, uint32_t *punBufferSizeUsed)
{
	return vr::TrackedProp_Success;
}
