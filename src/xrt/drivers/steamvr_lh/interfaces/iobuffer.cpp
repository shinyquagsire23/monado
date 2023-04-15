// Copyright 2023, Shawn Wallace
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief OpenVR IVRIOBuffer interface implementation.
 * @author Shawn Wallace <yungwallace@live.com>
 * @ingroup drv_steamvr_lh
 */

#include "iobuffer.hpp"

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
vr::EIOBufferError
IOBuffer::Open(const char *pchPath,
               vr::EIOBufferMode mode,
               uint32_t unElementSize,
               uint32_t unElements,
               vr::IOBufferHandle_t *pulBuffer)
{
	return vr::IOBuffer_Success;
}

vr::EIOBufferError
IOBuffer::Close(vr::IOBufferHandle_t ulBuffer)
{
	return vr::IOBuffer_Success;
}

vr::EIOBufferError
IOBuffer::Read(vr::IOBufferHandle_t ulBuffer, void *pDst, uint32_t unBytes, uint32_t *punRead)
{
	return vr::IOBuffer_Success;
}

vr::EIOBufferError
IOBuffer::Write(vr::IOBufferHandle_t ulBuffer, void *pSrc, uint32_t unBytes)
{
	return vr::IOBuffer_Success;
}

vr::PropertyContainerHandle_t
IOBuffer::PropertyContainer(vr::IOBufferHandle_t ulBuffer)
{
	return 1;
}

bool
IOBuffer::HasReaders(vr::IOBufferHandle_t ulBuffer)
{
	return false;
}
// NOLINTEND(bugprone-easily-swappable-parameters)
