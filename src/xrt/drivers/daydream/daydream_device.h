// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to Daydream driver code.
 * @author Pete Black <pete.black@collabora.com>
 * @ingroup drv_daydream
 */

#pragma once

#include "math/m_api.h"
#include "math/m_imu_pre.h"
#include "math/m_imu_3dof.h"

#include "xrt/xrt_device.h"

#include "os/os_threading.h"
#include "os/os_ble.h"


#ifdef __cplusplus
extern "C" {
#endif


/*!
 * A parsed sample of accel and gyro.
 */
struct daydream_parsed_sample
{
	struct xrt_vec3_i32 accel;
	struct xrt_vec3_i32 gyro;
	struct xrt_vec3_i32 mag;
};

enum daydream_button_bits
{
	DAYDREAM_TOUCHPAD_BUTTON_BIT = 0,
	DAYDREAM_CIRCLE_BUTTON_BIT = 1,
	DAYDREAM_BAR_BUTTON_BIT = 2,
	DAYDREAM_VOLUP_BUTTON_BIT = 3,
	DAYDREAM_VOLDN_BUTTON_BIT = 4,
};

enum daydream_button_masks
{
	DAYDREAM_TOUCHPAD_BUTTON_MASK = 1 << DAYDREAM_TOUCHPAD_BUTTON_BIT,
	DAYDREAM_CIRCLE_BUTTON_MASK = 1 << DAYDREAM_CIRCLE_BUTTON_BIT,
	DAYDREAM_BAR_BUTTON_MASK = 1 << DAYDREAM_BAR_BUTTON_BIT,
	DAYDREAM_VOLUP_BUTTON_MASK = 1 << DAYDREAM_VOLUP_BUTTON_BIT,
	DAYDREAM_VOLDN_BUTTON_MASK = 1 << DAYDREAM_VOLDN_BUTTON_BIT,
};

struct daydream_parsed_input
{
	uint8_t buttons;
	int timestamp;
	uint16_t timestamp_last;
	struct xrt_vec2_i32 touchpad;
	struct daydream_parsed_sample sample;
};

/*!
 * @implements xrt_device
 */
struct daydream_device
{
	struct xrt_device base;
	struct os_ble_device *ble;
	struct os_thread_helper oth;
	char mac[128];
	char path[128];

	struct
	{
		//! Lock for last and fusion.
		struct os_mutex lock;

		//! Last sensor read.
		struct daydream_parsed_input last;

		struct m_imu_pre_filter pre_filter;
		struct m_imu_3dof fusion;
	};

	bool print_spew;
	bool print_debug;

	struct
	{
		bool last;
	} gui;
};


struct daydream_device *
daydream_device_create(struct os_ble_device *ble,
                       bool print_spew,
                       bool print_debug);


#define DAYDREAM_SPEW(c, ...)                                                  \
	do {                                                                   \
		if (c->print_spew) {                                           \
			fprintf(stderr, "%s - ", __func__);                    \
			fprintf(stderr, __VA_ARGS__);                          \
			fprintf(stderr, "\n");                                 \
		}                                                              \
	} while (false)

#define DAYDREAM_DEBUG(c, ...)                                                 \
	do {                                                                   \
		if (c->print_debug) {                                          \
			fprintf(stderr, "%s - ", __func__);                    \
			fprintf(stderr, __VA_ARGS__);                          \
			fprintf(stderr, "\n");                                 \
		}                                                              \
	} while (false)

#define DAYDREAM_ERROR(c, ...)                                                 \
	do {                                                                   \
		fprintf(stderr, "%s - ", __func__);                            \
		fprintf(stderr, __VA_ARGS__);                                  \
		fprintf(stderr, "\n");                                         \
	} while (false)


#ifdef __cplusplus
}
#endif
