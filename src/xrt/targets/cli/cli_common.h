// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common file for the CLI program.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#pragma once


#ifdef __cplusplus
extern "C" {
#endif


int
cli_cmd_calibrate(int argc, const char **argv);

int
cli_cmd_lighthouse(int argc, const char **argv);

int
cli_cmd_probe(int argc, const char **argv);

int
cli_cmd_slambatch(int argc, const char **argv);

int
cli_cmd_test(int argc, const char **argv);

int
cli_cmd_trace(int argc, const char **argv);


#ifdef __cplusplus
}
#endif
