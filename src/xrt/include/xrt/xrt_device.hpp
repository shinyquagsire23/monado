// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  C++ helpers for xrt_device
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt_device.h"
#include "xrt_deleters.hpp"

#include <memory>

namespace xrt {


//! Unique-ownership smart pointer for a @ref xrt_device implementation.
using unique_xrt_device = std::unique_ptr<xrt_device, deleters::ptr_ptr_deleter<xrt_device, xrt_device_destroy>>;

} // namespace xrt
