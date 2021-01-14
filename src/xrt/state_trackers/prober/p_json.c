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

DEBUG_GET_ONCE_OPTION(active_config, "P_OVERRIDE_ACTIVE_CONFIG", NULL)


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

void
p_json_open_or_create_main_file(struct prober *p)
{
#ifdef XRT_OS_LINUX
	char tmp[1024];
	ssize_t ret = u_file_get_path_in_config_dir("config_v0.json", tmp, sizeof(tmp));
	if (ret <= 0) {
		U_LOG_E(
		    "Could not load or create config file no $HOME "
		    "or $XDG_CONFIG_HOME env variables defined");
		return;
	}

	FILE *file = u_file_open_file_in_config_dir("config_v0.json", "r");
	if (file == NULL) {
		return;
	}

	p->json.file_loaded = true;

	char *str = read_content(file);
	fclose(file);
	if (str == NULL) {
		U_LOG_E("Could not read the contents of '%s'!", tmp);
		return;
	}

	// No config created, ignore.
	if (strlen(str) == 0) {
		free(str);
		return;
	}

	p->json.root = cJSON_Parse(str);
	if (p->json.root == NULL) {
		U_LOG_E("Failed to parse JSON in '%s':\n%s\n#######", tmp, str);
		U_LOG_E("'%s'", cJSON_GetErrorPtr());
	}

	free(str);
#else
	//! @todo implement the underlying u_file_get_path_in_config_dir
	return;
#endif
}

static cJSON *
get_obj(cJSON *json, const char *name)
{
	cJSON *item = cJSON_GetObjectItemCaseSensitive(json, name);
	if (item == NULL) {
		U_LOG_E("Failed to find node '%s'!", name);
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
		U_LOG_E("Failed to parse '%s'!", name);
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
		U_LOG_E("Failed to parse '%s'!", name);
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
		U_LOG_E("Failed to parse '%s'!", name);
		return false;
	}

	return true;
}

static bool
is_json_ok(struct prober *p)
{
	if (p->json.root == NULL) {
		if (p->json.file_loaded) {
			U_LOG_E("JSON not parsed!");
		} else {
			U_LOG_W("No config file!");
		}
		return false;
	}

	return true;
}

static bool
parse_active(const char *str, const char *from, enum p_active_config *out_active)
{
	if (strcmp(str, "none") == 0) {
		*out_active = P_ACTIVE_CONFIG_NONE;
	} else if (strcmp(str, "tracking") == 0) {
		*out_active = P_ACTIVE_CONFIG_TRACKING;
	} else if (strcmp(str, "remote") == 0) {
		*out_active = P_ACTIVE_CONFIG_REMOTE;
	} else {
		U_LOG_E("Unknown active config '%s' from %s.", str, from);
		*out_active = P_ACTIVE_CONFIG_NONE;
		return false;
	}

	return true;
}

void
p_json_get_active(struct prober *p, enum p_active_config *out_active)
{
	const char *str = debug_get_option_active_config();
	if (str != NULL && parse_active(str, "environment", out_active)) {
		return;
	}

	char tmp[256];
	if (!is_json_ok(p) || !get_obj_str(p->json.root, "active", tmp, sizeof(tmp))) {
		*out_active = P_ACTIVE_CONFIG_NONE;
		return;
	}

	parse_active(tmp, "json", out_active);
}

bool
p_json_get_remote_port(struct prober *p, int *out_port)
{
	cJSON *t = cJSON_GetObjectItemCaseSensitive(p->json.root, "remote");
	if (t == NULL) {
		U_LOG_E("No remote node");
		return false;
	}

	int ver = -1;
	if (!get_obj_int(t, "version", &ver)) {
		U_LOG_E("Missing version tag!");
		return false;
	}
	if (ver >= 1) {
		U_LOG_E("Unknown version tag '%i'!", ver);
		return false;
	}

	int port = 0;
	if (!get_obj_int(t, "port", &port)) {
		return false;
	}

	*out_port = port;

	return true;
}

bool
p_json_get_tracking_settings(struct prober *p, struct xrt_settings_tracking *s)
{
	if (p->json.root == NULL) {
		if (p->json.file_loaded) {
			U_LOG_E("JSON not parsed!");
		} else {
			U_LOG_W("No config file!");
		}
		return false;
	}

	cJSON *t = cJSON_GetObjectItemCaseSensitive(p->json.root, "tracking");
	if (t == NULL) {
		U_LOG_E("No tracking node");
		return false;
	}

	char tmp[16];

	int ver = -1;
	bool bad = false;

	bad |= !get_obj_int(t, "version", &ver);
	if (bad || ver >= 1) {
		U_LOG_E("Missing or unknown version  tag '%i'", ver);
		return false;
	}

	bad |= !get_obj_str(t, "camera_name", s->camera_name, sizeof(s->camera_name));
	bad |= !get_obj_int(t, "camera_mode", &s->camera_mode);
	bad |= !get_obj_str(t, "camera_type", tmp, sizeof(tmp));
	bad |= !get_obj_str(t, "calibration_path", s->calibration_path, sizeof(s->calibration_path));
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
		U_LOG_W("Unknown camera type '%s'", tmp);
		return false;
	}

	return true;
}
