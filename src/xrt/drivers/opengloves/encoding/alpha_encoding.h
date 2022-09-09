// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenGloves Alpha Encoding Decoding interface.
 * @author Daniel Willmott <web@dan-w.com>
 * @ingroup drv_opengloves
 */

#pragma once
#include "encoding.h"

#ifdef __cplusplus
extern "C" {
#endif

void
opengloves_alpha_encoding_decode(const char *data, struct opengloves_input *out_kv);

void
opengloves_alpha_encoding_encode(const struct opengloves_output *output, char *out_buff);
#ifdef __cplusplus
}
#endif
