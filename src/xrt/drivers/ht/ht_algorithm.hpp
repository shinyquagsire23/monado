// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Camera based hand tracking mainloop algorithm.
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_ht
 */

#pragma once

struct ht_device;

void
htRunAlgorithm(struct ht_device *htd);
