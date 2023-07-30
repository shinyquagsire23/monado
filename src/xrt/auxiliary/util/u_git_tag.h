// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds the git hash of Monado.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


extern const char u_git_tag[];
extern const uint16_t u_version_major;
extern const uint16_t u_version_minor;
extern const uint16_t u_version_patch;


#ifdef __cplusplus
}
#endif
