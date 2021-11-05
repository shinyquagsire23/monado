// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Code to run machine learning models for camera-based hand tracker.
 * @author Moses Turner <moses@collabora.com>
 * @author Marcus Edel <marcus.edel@collabora.com>
 * @ingroup drv_ht
 */

#pragma once

struct ht_device;

void
initOnnx(struct ht_device *htd);

void
destroyOnnx(struct ht_device *htd);
