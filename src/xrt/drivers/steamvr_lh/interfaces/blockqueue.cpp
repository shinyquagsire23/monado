// Copyright 2023, Shawn Wallace
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief OpenVR IVRBlockQueue interface implementation.
 * @author Shawn Wallace <yungwallace@live.com>
 * @ingroup drv_steamvr_lh
 */

#include "blockqueue.hpp"


// NOLINTBEGIN(bugprone-easily-swappable-parameters)
vr::EBlockQueueError
BlockQueue::Create(vr::PropertyContainerHandle_t *pulQueueHandle,
                   char *pchPath,
                   uint32_t unBlockDataSize,
                   uint32_t unBlockHeaderSize,
                   uint32_t unBlockCount,
                   uint32_t unFlags)
{
	return vr::EBlockQueueError_BlockQueueError_None;
}

vr::EBlockQueueError
BlockQueue::Connect(vr::PropertyContainerHandle_t *pulQueueHandle, char *pchPath)
{
	return vr::EBlockQueueError_BlockQueueError_None;
}

vr::EBlockQueueError
BlockQueue::Destroy(vr::PropertyContainerHandle_t ulQueueHandle)
{
	return vr::EBlockQueueError_BlockQueueError_None;
}

vr::EBlockQueueError
BlockQueue::AcquireWriteOnlyBlock(vr::PropertyContainerHandle_t ulQueueHandle,
                                  vr::PropertyContainerHandle_t *pulBlockHandle,
                                  void **ppvBuffer)
{
	return vr::EBlockQueueError_BlockQueueError_None;
}

vr::EBlockQueueError
BlockQueue::ReleaseWriteOnlyBlock(vr::PropertyContainerHandle_t ulQueueHandle,
                                  vr::PropertyContainerHandle_t ulBlockHandle)
{
	return vr::EBlockQueueError_BlockQueueError_None;
}

vr::EBlockQueueError
BlockQueue::WaitAndAcquireReadOnlyBlock(vr::PropertyContainerHandle_t ulQueueHandle,
                                        vr::PropertyContainerHandle_t *pulBlockHandle,
                                        void **ppvBuffer,
                                        vr::EBlockQueueReadType eReadType,
                                        uint32_t unTimeoutMs)
{
	return vr::EBlockQueueError_BlockQueueError_None;
}

vr::EBlockQueueError
BlockQueue::AcquireReadOnlyBlock(vr::PropertyContainerHandle_t ulQueueHandle,
                                 vr::PropertyContainerHandle_t *pulBlockHandle,
                                 void **ppvBuffer,
                                 vr::EBlockQueueReadType eReadType)
{
	return vr::EBlockQueueError_BlockQueueError_None;
}

vr::EBlockQueueError
BlockQueue::ReleaseReadOnlyBlock(vr::PropertyContainerHandle_t ulQueueHandle,
                                 vr::PropertyContainerHandle_t ulBlockHandle)
{
	return vr::EBlockQueueError_BlockQueueError_None;
}

vr::EBlockQueueError
BlockQueue::QueueHasReader(vr::PropertyContainerHandle_t ulQueueHandle, bool *pbHasReaders)

{
	return vr::EBlockQueueError_BlockQueueError_None;
}
// NOLINTEND(bugprone-easily-swappable-parameters)
