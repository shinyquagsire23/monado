// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  C++ helpers for xrt_device
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt_device.h"

#include <memory>

namespace xrt {

namespace deleters {
	//! Deleter type for xrt_device
	struct xrt_device_deleter
	{
		void
		operator()(xrt_device *dev) const noexcept
		{
			xrt_device_destroy(&dev);
		}
	};

} // namespace deleters

//! Unique-ownership smart pointer for a @ref xrt_device implementation.
using unique_xrt_device = std::unique_ptr<xrt_device, deleters::xrt_device_deleter>;

} // namespace xrt
