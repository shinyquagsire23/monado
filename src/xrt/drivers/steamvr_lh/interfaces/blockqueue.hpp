// Copyright 2023, Shawn Wallace
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief OpenVR IVRBlockQueue interface header.
 * @author Shawn Wallace <yungwallace@live.com>
 * @ingroup drv_steamvr_lh
 */

#pragma once

#include "openvr_driver.h"

/** Definitions missing from C++ header, present in C */
namespace vr {
inline const char *IVRBlockQueue_Version = "IVRBlockQueue_005";

typedef enum EBlockQueueError
{
	EBlockQueueError_BlockQueueError_None = 0,
	EBlockQueueError_BlockQueueError_QueueAlreadyExists = 1,
	EBlockQueueError_BlockQueueError_QueueNotFound = 2,
	EBlockQueueError_BlockQueueError_BlockNotAvailable = 3,
	EBlockQueueError_BlockQueueError_InvalidHandle = 4,
	EBlockQueueError_BlockQueueError_InvalidParam = 5,
	EBlockQueueError_BlockQueueError_ParamMismatch = 6,
	EBlockQueueError_BlockQueueError_InternalError = 7,
	EBlockQueueError_BlockQueueError_AlreadyInitialized = 8,
	EBlockQueueError_BlockQueueError_OperationIsServerOnly = 9,
	EBlockQueueError_BlockQueueError_TooManyConnections = 10,
} EBlockQueueError;

typedef enum EBlockQueueReadType
{
	EBlockQueueReadType_BlockQueueRead_Latest = 0,
	EBlockQueueReadType_BlockQueueRead_New = 1,
	EBlockQueueReadType_BlockQueueRead_Next = 2,
} EBlockQueueReadType;
} // namespace vr

/** This interface is missing in the C++ header but present in the C one, and the lighthouse driver requires it. */
class BlockQueue
{
public:
	virtual vr::EBlockQueueError
	Create(vr::PropertyContainerHandle_t *pulQueueHandle,
	       char *pchPath,
	       uint32_t unBlockDataSize,
	       uint32_t unBlockHeaderSize,
	       uint32_t unBlockCount,
	       uint32_t unFlags);
	virtual vr::EBlockQueueError
	Connect(vr::PropertyContainerHandle_t *pulQueueHandle, char *pchPath);
	virtual vr::EBlockQueueError
	Destroy(vr::PropertyContainerHandle_t ulQueueHandle);
	virtual vr::EBlockQueueError
	AcquireWriteOnlyBlock(vr::PropertyContainerHandle_t ulQueueHandle,
	                      vr::PropertyContainerHandle_t *pulBlockHandle,
	                      void **ppvBuffer);
	virtual vr::EBlockQueueError
	ReleaseWriteOnlyBlock(vr::PropertyContainerHandle_t ulQueueHandle, vr::PropertyContainerHandle_t ulBlockHandle);
	virtual vr::EBlockQueueError
	WaitAndAcquireReadOnlyBlock(vr::PropertyContainerHandle_t ulQueueHandle,
	                            vr::PropertyContainerHandle_t *pulBlockHandle,
	                            void **ppvBuffer,
	                            vr::EBlockQueueReadType eReadType,
	                            uint32_t unTimeoutMs);
	virtual vr::EBlockQueueError
	AcquireReadOnlyBlock(vr::PropertyContainerHandle_t ulQueueHandle,
	                     vr::PropertyContainerHandle_t *pulBlockHandle,
	                     void **ppvBuffer,
	                     vr::EBlockQueueReadType eReadType);
	virtual vr::EBlockQueueError
	ReleaseReadOnlyBlock(vr::PropertyContainerHandle_t ulQueueHandle, vr::PropertyContainerHandle_t ulBlockHandle);
	virtual vr::EBlockQueueError
	QueueHasReader(vr::PropertyContainerHandle_t ulQueueHandle, bool *pbHasReaders);
};
