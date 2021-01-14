// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Very simple file opening functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Pete Black <pblack@collabora.com>
 * @ingroup aux_util
 */

#include "xrt/xrt_config_os.h"
#include "util/u_file.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>


#ifdef XRT_OS_LINUX
#include <linux/limits.h>

static int
mkpath(const char *path)
{
	char tmp[PATH_MAX];
	char *p = NULL;
	size_t len;

	snprintf(tmp, sizeof(tmp), "%s", path);
	len = strlen(tmp) - 1;
	if (tmp[len] == '/') {
		tmp[len] = 0;
	}

	for (p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = 0;
			if (mkdir(tmp, S_IRWXU) < 0 && errno != EEXIST) {
				return -1;
			}
			*p = '/';
		}
	}

	if (mkdir(tmp, S_IRWXU) < 0 && errno != EEXIST) {
		return -1;
	}

	return 0;
}

ssize_t
u_file_get_config_dir(char *out_path, size_t out_path_size)
{
	const char *xgd_home = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (xgd_home != NULL) {
		return snprintf(out_path, out_path_size, "%s/monado", xgd_home);
	}
	if (home != NULL) {
		return snprintf(out_path, out_path_size, "%s/.config/monado", home);
	}
	return -1;
}

ssize_t
u_file_get_path_in_config_dir(const char *filename, char *out_path, size_t out_path_size)
{
	char tmp[PATH_MAX];
	ssize_t i = u_file_get_config_dir(tmp, sizeof(tmp));
	if (i <= 0) {
		return -1;
	}

	return snprintf(out_path, out_path_size, "%s/%s", tmp, filename);
}

FILE *
u_file_open_file_in_config_dir(const char *filename, const char *mode)
{
	char tmp[PATH_MAX];
	ssize_t i = u_file_get_config_dir(tmp, sizeof(tmp));
	if (i <= 0) {
		return NULL;
	}

	char file_str[PATH_MAX + 15];
	i = snprintf(file_str, sizeof(file_str), "%s/%s", tmp, filename);
	if (i <= 0) {
		return NULL;
	}

	FILE *file = fopen(file_str, mode);
	if (file != NULL) {
		return file;
	}

	// Try creating the path.
	mkpath(tmp);

	// Do not report error.
	return fopen(file_str, mode);
}

#endif
