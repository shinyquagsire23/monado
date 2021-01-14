// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Hid implementation based on hidraw.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_os
 */

#include "os_hid.h"

#ifdef XRT_OS_LINUX

#include "util/u_misc.h"
#include <poll.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include <sys/ioctl.h>

#include <linux/hidraw.h>


#include <stdio.h>

/*!
 * @implements os_hid_device
 */
struct hid_hidraw
{
	struct os_hid_device base;

	int fd;
};

static int
os_hidraw_read(struct os_hid_device *ohdev, uint8_t *data, size_t length, int milliseconds)
{
	struct hid_hidraw *hrdev = (struct hid_hidraw *)ohdev;
	struct pollfd fds;
	int ret;

	if (milliseconds >= 0) {
		fds.fd = hrdev->fd;
		fds.events = POLLIN;
		fds.revents = 0;
		ret = poll(&fds, 1, milliseconds);

		if (ret == -1 || ret == 0) {
			// Error or timeout.
			return ret;
		}
		if (fds.revents & (POLLERR | POLLHUP | POLLNVAL)) {
			// Device disconnect?
			return -1;
		}
	}

	ret = read(hrdev->fd, data, length);

	if (ret < 0 && (errno == EAGAIN || errno == EINPROGRESS)) {
		// Process most likely received a signal.
		ret = 0;
	}

	return ret;
}

static int
os_hidraw_write(struct os_hid_device *ohdev, const uint8_t *data, size_t length)
{
	struct hid_hidraw *hrdev = (struct hid_hidraw *)ohdev;

	return write(hrdev->fd, data, length);
}

static int
os_hidraw_get_feature(struct os_hid_device *ohdev, uint8_t report_num, uint8_t *data, size_t length)
{
	struct hid_hidraw *hrdev = (struct hid_hidraw *)ohdev;
	// The ioctl expects the report number in the first byte of the buffer,
	// but it will overwrite it.
	data[0] = report_num;
	return ioctl(hrdev->fd, HIDIOCGFEATURE(length), data);
}

static int
os_hidraw_get_feature_timeout(struct os_hid_device *ohdev, void *data, size_t length, uint32_t timeout)
{
	struct hid_hidraw *hrdev = (struct hid_hidraw *)ohdev;

	struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000};
	unsigned int i;
	int ret = 0;

	for (i = 0; i < timeout; i++) {
		ret = ioctl(hrdev->fd, HIDIOCGFEATURE(length), data);
		if (ret != -1 || errno != EPIPE)
			break;

		ts.tv_sec = 0;
		ts.tv_nsec = 1000000;
		nanosleep(&ts, NULL);
	}

	return ret;
}

static int
os_hidraw_set_feature(struct os_hid_device *ohdev, const uint8_t *data, size_t length)
{
	struct hid_hidraw *hrdev = (struct hid_hidraw *)ohdev;

	return ioctl(hrdev->fd, HIDIOCSFEATURE(length), data);
}

static void
os_hidraw_destroy(struct os_hid_device *ohdev)
{
	struct hid_hidraw *hrdev = (struct hid_hidraw *)ohdev;

	close(hrdev->fd);
	free(hrdev);
}

int
os_hid_open_hidraw(const char *path, struct os_hid_device **out_hid)
{
	struct hid_hidraw *hrdev = U_TYPED_CALLOC(struct hid_hidraw);

	hrdev->base.read = os_hidraw_read;
	hrdev->base.write = os_hidraw_write;
	hrdev->base.get_feature = os_hidraw_get_feature;
	hrdev->base.get_feature_timeout = os_hidraw_get_feature_timeout;
	hrdev->base.set_feature = os_hidraw_set_feature;
	hrdev->base.destroy = os_hidraw_destroy;
	hrdev->fd = open(path, O_RDWR);
	if (hrdev->fd < 0) {
		free(hrdev);
		return -errno;
	}

	*out_hid = &hrdev->base;

	return 0;
}

#endif
