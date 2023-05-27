// Copyright 2019-2022, Collabora, Ltd.
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


#if defined(XRT_OS_WINDOWS) && !defined(XRT_ENV_MINGW)
#define PATH_MAX MAX_PATH
#endif

#ifdef XRT_OS_LINUX
#include <sys/stat.h>
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

static bool
is_dir(const char *path)
{
	struct stat st = {0};
	if (!stat(path, &st)) {
		return S_ISDIR(st.st_mode);
	} else {
		return false;
	}
}

ssize_t
u_file_get_config_dir(char *out_path, size_t out_path_size)
{
	const char *xdg_home = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (xdg_home != NULL) {
		return snprintf(out_path, out_path_size, "%s/monado", xdg_home);
	}
	if (home != NULL) {
		return snprintf(out_path, out_path_size, "%s/.config/monado", home);
	}
	return -1;
}

ssize_t
u_file_get_path_in_config_dir(const char *suffix, char *out_path, size_t out_path_size)
{
	char tmp[PATH_MAX];
	ssize_t i = u_file_get_config_dir(tmp, sizeof(tmp));
	if (i <= 0) {
		return -1;
	}

	return snprintf(out_path, out_path_size, "%s/%s", tmp, suffix);
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

FILE *
u_file_open_file_in_config_dir_subpath(const char *subpath, const char *filename, const char *mode)
{
	char tmp[PATH_MAX];
	int i = u_file_get_config_dir(tmp, sizeof(tmp));
	if (i < 0 || i >= (int)sizeof(tmp)) {
		return NULL;
	}

	char fullpath[PATH_MAX];
	i = snprintf(fullpath, sizeof(fullpath), "%s/%s", tmp, subpath);
	if (i < 0 || i >= (int)sizeof(fullpath)) {
		return NULL;
	}

	char file_str[PATH_MAX + 15];
	i = snprintf(file_str, sizeof(file_str), "%s/%s", fullpath, filename);
	if (i < 0 || i >= (int)sizeof(file_str)) {
		return NULL;
	}

	FILE *file = fopen(file_str, mode);
	if (file != NULL) {
		return file;
	}

	// Try creating the path.
	mkpath(fullpath);

	// Do not report error.
	return fopen(file_str, mode);
}

ssize_t
u_file_get_hand_tracking_models_dir(char *out_path, size_t out_path_size)
{
	const char *suffix = "/monado/hand-tracking-models";
	const char *xdg_data_home = getenv("XDG_DATA_HOME");
	const char *home = getenv("HOME");
	ssize_t ret = 0;

	if (xdg_data_home != NULL) {
		ret = snprintf(out_path, out_path_size, "%s%s", xdg_data_home, suffix);
		if (ret > 0 && is_dir(out_path)) {
			return ret;
		}
	}

	if (home != NULL) {
		ret = snprintf(out_path, out_path_size, "%s/.local/share%s", home, suffix);
		if (ret > 0 && is_dir(out_path)) {
			return ret;
		}
	}

	ret = snprintf(out_path, out_path_size, "/usr/local/share%s", suffix);
	if (ret > 0 && is_dir(out_path)) {
		return ret;
	}

	ret = snprintf(out_path, out_path_size, "/usr/share%s", suffix);
	if (ret > 0 && is_dir(out_path)) {
		return ret;
	}

	if (out_path_size > 0) {
		out_path[0] = '\0';
	}

	return -1;
}

#endif /* XRT_OS_LINUX */

ssize_t
u_file_get_runtime_dir(char *out_path, size_t out_path_size)
{
	const char *xgd_rt = getenv("XDG_RUNTIME_DIR");
	if (xgd_rt != NULL) {
		return snprintf(out_path, out_path_size, "%s", xgd_rt);
	}

	const char *tmp = "/tmp";
	return snprintf(out_path, out_path_size, "%s", tmp);
}

ssize_t
u_file_get_path_in_runtime_dir(const char *suffix, char *out_path, size_t out_path_size)
{
	char tmp[PATH_MAX];
	ssize_t i = u_file_get_runtime_dir(tmp, sizeof(tmp));
	if (i <= 0) {
		return -1;
	}

	return snprintf(out_path, out_path_size, "%s/%s", tmp, suffix);
}

char *
u_file_read_content(FILE *file)
{
	// Go to the end of the file.
	fseek(file, 0L, SEEK_END);
	size_t file_size = ftell(file);

	// Return back to the start of the file.
	fseek(file, 0L, SEEK_SET);

	char *buffer = (char *)calloc(file_size + 1, sizeof(char));
	if (buffer == NULL) {
		return NULL;
	}

	// Do the actual reading.
	size_t ret = fread(buffer, sizeof(char), file_size, file);
	if (ret != file_size) {
		free(buffer);
		return NULL;
	}

	return buffer;
}

char *
u_file_read_content_from_path(const char *path)
{
	FILE *file = fopen(path, "r");
	if (file == NULL) {
		return NULL;
	}
	char *file_content = u_file_read_content(file);
	int ret = fclose(file);
	// We don't care about the return value since we're just reading
	(void)ret;

	// Either valid non-null or null
	return file_content;
}
