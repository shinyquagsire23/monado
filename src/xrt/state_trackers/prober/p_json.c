// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Code to manage the settings file.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup st_prober
 */

#include "util/u_file.h"
#include "util/u_json.h"
#include "util/u_debug.h"
#include "p_prober.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>


char *
read_content(FILE *file)
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

cJSON *
p_json_open_or_create_main_file(void)
{
	FILE *file = u_file_open_file_in_config_dir("config_v0.json", "r");
	if (file == NULL) {
		fprintf(stderr, "Could not open the file!\n");
		return NULL;
	}

	char *str = read_content(file);
	fclose(file);
	if (str == NULL) {
		fprintf(stderr, "Could not read the contents of the file!\n");
		return NULL;
	}

	cJSON *ret = cJSON_Parse(str);
	if (ret == NULL) {
		fprintf(stderr, "Failed to parse JSON:\n%s\n#######\n", str);
		fprintf(stderr, "'%s'\n", cJSON_GetErrorPtr());
	}

	free(str);
	return ret;
}

static cJSON *
get_obj(cJSON *json, const char *name)
{
	cJSON *item = cJSON_GetObjectItemCaseSensitive(json, name);
	if (item == NULL) {
		fprintf(stderr, "Failed to find node '%s'!\n", name);
	}
	return item;
}

XRT_MAYBE_UNUSED static bool
get_obj_bool(cJSON *json, const char *name, bool *out_bool)
{
	cJSON *item = get_obj(json, name);
	if (item == NULL) {
		return false;
	}

	if (!u_json_get_bool(item, out_bool)) {
		fprintf(stderr, "Failed to parse '%s'!\n", name);
		return false;
	}

	return true;
}

static bool
get_obj_int(cJSON *json, const char *name, int *out_int)
{
	cJSON *item = get_obj(json, name);
	if (item == NULL) {
		return false;
	}

	if (!u_json_get_int(item, out_int)) {
		fprintf(stderr, "Failed to parse '%s'!\n", name);
		return false;
	}

	return true;
}

static bool
get_obj_str(cJSON *json, const char *name, char *array, size_t array_size)
{
	cJSON *item = get_obj(json, name);
	if (item == NULL) {
		return false;
	}

	if (!u_json_get_string_into_array(item, array, array_size)) {
		fprintf(stderr, "Failed to parse '%s'!\n", name);
		return false;
	}

	return true;
}

bool
p_json_get_tracking_settings(cJSON *root, struct xrt_settings_tracking *s)
{
	if (root == NULL) {
		return false;
	}

	cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "tracking");
	if (t == NULL) {
		fprintf(stderr, "No tracking node!\n");
		return false;
	}

	char tmp[16];

	int ver = -1;
	bool bad = false;

	bad |= !get_obj_int(t, "version", &ver);
	if (bad || ver >= 1) {
		fprintf(stderr, "Missing or unknown version  tag '%i'\n", ver);
		return false;
	}

	bad |= !get_obj_str(t, "camera_name", s->camera_name,
	                    sizeof(s->camera_name));
	bad |= !get_obj_int(t, "camera_mode", &s->camera_mode);
	bad |= !get_obj_str(t, "camera_type", tmp, sizeof(tmp));
	bad |= !get_obj_str(t, "calibration_path", s->calibration_path,
	                    sizeof(s->calibration_path));
	if (bad) {
		return false;
	}

	if (strcmp(tmp, "regular_mono") == 0) {
		s->camera_type = XRT_SETTINGS_CAMERA_TYPE_REGULAR_MONO;
	} else if (strcmp(tmp, "regular_sbs") == 0) {
		s->camera_type = XRT_SETTINGS_CAMERA_TYPE_REGULAR_SBS;
	} else if (strcmp(tmp, "ps4") == 0) {
		s->camera_type = XRT_SETTINGS_CAMERA_TYPE_PS4;
	} else if (strcmp(tmp, "leap_motion") == 0) {
		s->camera_type = XRT_SETTINGS_CAMERA_TYPE_LEAP_MOTION;
	} else {
		fprintf(stderr, "Unknown camera type '%s'\n", tmp);
		return false;
	}

	return true;
}
