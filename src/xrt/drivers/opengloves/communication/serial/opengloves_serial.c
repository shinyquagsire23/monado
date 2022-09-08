// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  USB Serial implementation for OpenGloves.
 * @author Daniel Willmott <web@dan-w.com>
 * @ingroup drv_opengloves
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>

#include "opengloves_serial.h"

#include "util/u_misc.h"


static int
opengloves_serial_read(struct opengloves_communication_device *ocdev, char *data, size_t length)
{
	struct opengloves_serial_device *osdev = (struct opengloves_serial_device *)ocdev;
	int ret = read(osdev->fd, data, length);

	return ret;
}

static int
opengloves_serial_write(struct opengloves_communication_device *ocdev, const char *data, size_t length)
{
	struct opengloves_serial_device *osdev = (struct opengloves_serial_device *)ocdev;

	return write(osdev->fd, data, length);
}

static void
opengloves_serial_destroy(struct opengloves_communication_device *ocdev)
{
	struct opengloves_serial_device *osdev = (struct opengloves_serial_device *)ocdev;

	close(osdev->fd);
	free(osdev);
}

int
opengloves_serial_open(const char *path, struct opengloves_communication_device **out_comm_dev)
{
	int fd = open(path, O_RDWR);

	// error opening file
	if (fd < 0) {
		return -errno;
	}

	// read existing settings
	struct termios tty;
	if (tcgetattr(fd, &tty) != 0) {
		return -errno;
	}

	tty.c_cflag &= ~PARENB;
	tty.c_cflag &= ~CSTOPB;
	tty.c_cflag &= ~CSIZE;
	tty.c_cflag |= CS8;
	tty.c_cflag |= CREAD | CLOCAL;
	tty.c_cflag &= ~CRTSCTS;

	tty.c_lflag &= ~ICANON;
	tty.c_lflag &= ~ECHO;
	tty.c_lflag &= ~ECHOE;
	tty.c_lflag &= ~ECHONL;
	tty.c_lflag &= ~ISIG;
	tty.c_iflag &= ~(IXON | IXOFF | IXANY);
	tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

	tty.c_oflag &= ~OPOST;
	tty.c_oflag &= ~ONLCR;

	tty.c_cc[VTIME] = 10;
	tty.c_cc[VMIN] = 0;

	// baud rates
	cfsetispeed(&tty, B115200);
	cfsetospeed(&tty, B115200);

	if (tcsetattr(fd, TCSANOW, &tty) != 0) {
		return -errno;
	}

	struct opengloves_serial_device *osdev = U_TYPED_CALLOC(struct opengloves_serial_device);

	osdev->base.read = opengloves_serial_read;
	osdev->base.write = opengloves_serial_write;
	osdev->base.destroy = opengloves_serial_destroy;

	osdev->fd = fd;

	*out_comm_dev = &osdev->base;

	return 0;
}
