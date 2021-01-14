// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief low level libsurvive wrapper
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @ingroup drv_survive
 */

#pragma once

#include "survive_api.h"

bool
survive_has_obj(const SurviveSimpleObject *sso);

bool
survive_config_ready(const SurviveSimpleObject *sso);

char *
survive_get_json_config(const SurviveSimpleObject *sso);
