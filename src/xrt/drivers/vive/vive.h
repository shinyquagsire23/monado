// Copyright 2016-2019, Philipp Zabel
// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common vive header
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup drv_vive
 */

#pragma once

/*
 *
 * Printing functions.
 *
 */

#define VIVE_TRACE(d, ...) U_LOG_XDEV_IFL_T(&d->base, d->ll, __VA_ARGS__)
#define VIVE_DEBUG(d, ...) U_LOG_XDEV_IFL_D(&d->base, d->ll, __VA_ARGS__)
#define VIVE_INFO(d, ...) U_LOG_XDEV_IFL_I(&d->base, d->ll, __VA_ARGS__)
#define VIVE_WARN(d, ...) U_LOG_XDEV_IFL_W(&d->base, d->ll, __VA_ARGS__)
#define VIVE_ERROR(d, ...) U_LOG_XDEV_IFL_E(&d->base, d->ll, __VA_ARGS__)
