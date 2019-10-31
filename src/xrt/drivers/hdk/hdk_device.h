// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to direct OSVR HDK driver code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup drv_hdk
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

enum HDK_VARIANT
{
	HDK_UNKNOWN = 0,
	HDK_VARIANT_1_2,
	HDK_VARIANT_1_3_1_4,
	HDK_VARIANT_2
};

struct hdk_device
{
	struct xrt_device base;
	struct os_hid_device *dev;
	enum HDK_VARIANT variant;

	bool print_spew;
	bool print_debug;
	bool disconnect_notified;
};

static inline struct hdk_device *
hdk_device(struct xrt_device *xdev)
{
	return (struct hdk_device *)xdev;
}

struct hdk_device *
hdk_device_create(struct os_hid_device *dev,
                  enum HDK_VARIANT variant,
                  bool print_spew,
                  bool print_debug);

#define HDK_SPEW(c, ...)                                                       \
	do {                                                                   \
		if (c->print_spew) {                                           \
			fprintf(stderr, "%s - ", __func__);                    \
			fprintf(stderr, __VA_ARGS__);                          \
			fprintf(stderr, "\n");                                 \
		}                                                              \
	} while (false)
#define HDK_DEBUG(c, ...)                                                      \
	do {                                                                   \
		if (c->print_debug) {                                          \
			fprintf(stderr, "%s - ", __func__);                    \
			fprintf(stderr, __VA_ARGS__);                          \
			fprintf(stderr, "\n");                                 \
		}                                                              \
	} while (false)

#define HDK_ERROR(c, ...)                                                      \
	do {                                                                   \
		fprintf(stderr, "%s - ", __func__);                            \
		fprintf(stderr, __VA_ARGS__);                                  \
		fprintf(stderr, "\n");                                         \
	} while (false)


#ifdef __cplusplus
}
#endif
