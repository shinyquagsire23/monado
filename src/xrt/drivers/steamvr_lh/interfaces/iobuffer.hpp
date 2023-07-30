// Copyright 2023, Shawn Wallace
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief OpenVR IVRIOBuffer interface header.
 * @author Shawn Wallace <yungwallace@live.com>
 * @ingroup drv_steamvr_lh
 */

#pragma once

#include "openvr_driver.h"

class IOBuffer : public vr::IVRIOBuffer
{
public:
	/** opens an existing or creates a new IOBuffer of unSize bytes */
	vr::EIOBufferError
	Open(const char *pchPath,
	     vr::EIOBufferMode mode,
	     uint32_t unElementSize,
	     uint32_t unElements,
	     vr::IOBufferHandle_t *pulBuffer) override;

	/** closes a previously opened or created buffer */
	vr::EIOBufferError
	Close(vr::IOBufferHandle_t ulBuffer) override;

	/** reads up to unBytes from buffer into *pDst, returning number of bytes read in *punRead */
	vr::EIOBufferError
	Read(vr::IOBufferHandle_t ulBuffer, void *pDst, uint32_t unBytes, uint32_t *punRead) override;

	/** writes unBytes of data from *pSrc into a buffer. */
	vr::EIOBufferError
	Write(vr::IOBufferHandle_t ulBuffer, void *pSrc, uint32_t unBytes) override;

	/** retrieves the property container of an buffer. */
	vr::PropertyContainerHandle_t
	PropertyContainer(vr::IOBufferHandle_t ulBuffer) override;

	/** inexpensively checks for readers to allow writers to fast-fail potentially expensive copies and writes. */
	bool
	HasReaders(vr::IOBufferHandle_t ulBuffer) override;
};
