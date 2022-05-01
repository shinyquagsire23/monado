// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Very simple file opening functions, mostly using std::filesystem for portability.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Pete Black <pblack@collabora.com>
 * @ingroup aux_util
 */

#include "xrt/xrt_config_os.h"
#include "util/u_file.h"

#ifndef XRT_OS_LINUX

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if __cplusplus >= 201703L
#include <filesystem>
namespace fs = std::filesystem;
#else
#define _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif

static inline fs::path
get_config_path()
{
#ifdef XRT_OS_WINDOWS
	char *buffer = nullptr;
	errno_t ret = _dupenv_s(&buffer, nullptr, "LOCALAPPDATA");
	if (ret != 0) {
		return {};
	}

	auto local_app_data = fs::path{buffer};
	free(buffer);

	return local_app_data / "monado";
#else
	const char *xdg_home = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (xdg_home != NULL) {
		return fs::path(xdg_home) / "monado";
	}
	if (home != NULL) {
		return fs::path(home) / "monado";
	}
	return {};
#endif
}

ssize_t
u_file_get_config_dir(char *out_path, size_t out_path_size)
{
	auto config_path = get_config_path();
	if (config_path.empty()) {
		return -1;
	}
	auto config_path_string = config_path.string();
	return snprintf(out_path, out_path_size, "%s", config_path_string.c_str());
}

ssize_t
u_file_get_path_in_config_dir(const char *filename, char *out_path, size_t out_path_size)
{
	auto config_path = get_config_path();
	if (config_path.empty()) {
		return -1;
	}
	auto path_string = (config_path / filename).string();
	return snprintf(out_path, out_path_size, "%s", path_string.c_str());
}

FILE *
u_file_open_file_in_config_dir(const char *filename, const char *mode)
{
	auto config_path = get_config_path();
	if (config_path.empty()) {
		return NULL;
	}

	auto file_path_string = (config_path / filename).string();
	FILE *file = nullptr;
	errno_t ret = fopen_s(&file, file_path_string.c_str(), mode);
	if (ret == 0) {
		return file;
	}

	// Try creating the path.
	auto directory = (config_path / filename).parent_path();
	fs::create_directories(directory);

	// Do not report error.
	ret = fopen_s(&file, file_path_string.c_str(), mode);
	if (ret == 0) {
		return file;
	}

	return nullptr;
}

#endif // XRT_OS_LINUX
