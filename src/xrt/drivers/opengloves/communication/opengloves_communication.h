// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Communication structures for OpenGloves
 * @author Daniel Willmott <web@dan-w.com>
 * @ingroup drv_opengloves
 */

#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @interface opengloves_communication_device
 *
 * Interface for a communication method
 *
 * @ingroup drv_opengloves
 */
struct opengloves_communication_device
{
	int (*read)(struct opengloves_communication_device *comm_dev, char *data, size_t size);

	int (*write)(struct opengloves_communication_device *comm_dev, const char *data, size_t size);

	void (*destroy)(struct opengloves_communication_device *comm_dev);
};

static inline int
opengloves_communication_device_read(struct opengloves_communication_device *comm_dev, char *data, size_t size)
{
	return comm_dev->read(comm_dev, data, size);
}

static inline int
opengloves_communication_device_write(struct opengloves_communication_device *comm_dev, const char *data, size_t size)
{
	return comm_dev->write(comm_dev, data, size);
}


static inline void
opengloves_communication_device_destory(struct opengloves_communication_device *comm_dev)
{
	comm_dev->destroy(comm_dev);
}


#ifdef __cplusplus
}
#endif
