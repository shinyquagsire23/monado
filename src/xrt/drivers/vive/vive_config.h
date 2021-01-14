// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  vive json header
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup drv_vive
 */

#pragma once

#include <stdbool.h>

struct vive_device;

bool
vive_config_parse(struct vive_device *d, char *json_string);


struct vive_controller_device;

bool
vive_config_parse_controller(struct vive_controller_device *d, char *json_string);
