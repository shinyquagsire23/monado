// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Code to manage the settings file.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup st_prober
 */

#include <xrt/xrt_device.h>
#include "xrt/xrt_settings.h"
#include "xrt/xrt_config.h"

#include "util/u_file.h"
#include "util/u_json.h"
#include "util/u_debug.h"

#include "u_config_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "bindings/b_generated_bindings.h"
#include <assert.h>

DEBUG_GET_ONCE_OPTION(active_config, "P_OVERRIDE_ACTIVE_CONFIG", NULL)

#define CONFIG_FILE_NAME "config_v0.json"
#define GUI_STATE_FILE_NAME "gui_state_v0.json"

void
u_config_json_close(struct u_config_json *json)
{
	if (json->root != NULL) {
		cJSON_Delete(json->root);
		json->root = NULL;
	}
	json->file_loaded = false;
}

static void
u_config_json_open_or_create_file(struct u_config_json *json, const char *filename)
{
	json->file_loaded = false;
#if (defined(XRT_OS_LINUX) || defined(XRT_OS_WINDOWS)) && !defined(XRT_OS_ANDROID)
	char tmp[1024];
	ssize_t ret = u_file_get_path_in_config_dir(filename, tmp, sizeof(tmp));
	if (ret <= 0) {
		U_LOG_E(
		    "Could not load or create config file no $HOME "
		    "or $XDG_CONFIG_HOME env variables defined");
		return;
	}

	FILE *file = u_file_open_file_in_config_dir(filename, "r");
	if (file == NULL) {
		return;
	}

	json->file_loaded = true;

	char *str = u_file_read_content(file);
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

	json->root = cJSON_Parse(str);
	if (json->root == NULL) {
		U_LOG_E("Failed to parse JSON in '%s':\n%s\n#######", tmp, str);
		U_LOG_E("'%s'", cJSON_GetErrorPtr());
	}

	free(str);
#else
	//! @todo implement the underlying u_file_get_path_in_config_dir
	return;
#endif
}

void
u_config_json_open_or_create_main_file(struct u_config_json *json)
{
	u_config_json_open_or_create_file(json, CONFIG_FILE_NAME);
}

static cJSON *
get_obj(cJSON *json, const char *name)
{
	cJSON *item = cJSON_GetObjectItemCaseSensitive(json, name);
	if (item == NULL) {
		U_LOG_I("JSON does not contain node '%s'!", name);
	}
	return item;
}

XRT_MAYBE_UNUSED static bool
get_obj_bool(cJSON *json, const char *name, bool *out_bool)
{
	if (json == NULL) {
		return false;
	}
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
	if (json == NULL) {
		return false;
	}
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
get_obj_float(cJSON *json, const char *name, float *out_float)
{
	if (json == NULL) {
		return false;
	}
	cJSON *item = get_obj(json, name);
	if (item == NULL) {
		return false;
	}

	if (!u_json_get_float(item, out_float)) {
		U_LOG_E("Failed to parse '%s'!", name);
		return false;
	}

	return true;
}

static bool
get_obj_str(cJSON *json, const char *name, char *array, size_t array_size)
{
	if (json == NULL) {
		return false;
	}
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
is_json_ok(struct u_config_json *json)
{
	if (json->root == NULL) {
		if (json->file_loaded) {
			U_LOG_E("Config file was loaded but JSON is not parsed!");
		} else {
			U_LOG_I("No config file was loaded!");
		}
		return false;
	}

	return true;
}

static void
u_config_json_assign_schema(struct u_config_json *json)
{
	cJSON_DeleteItemFromObject(json->root, "$schema");
	cJSON_AddStringToObject(json->root, "$schema",
	                        "https://monado.pages.freedesktop.org/monado/config_v0.schema.json");
}

static bool
parse_active(const char *str, const char *from, enum u_config_json_active_config *out_active)
{
	if (strcmp(str, "none") == 0) {
		*out_active = U_ACTIVE_CONFIG_NONE;
	} else if (strcmp(str, "tracking") == 0) {
		*out_active = U_ACTIVE_CONFIG_TRACKING;
	} else if (strcmp(str, "remote") == 0) {
		*out_active = U_ACTIVE_CONFIG_REMOTE;
	} else {
		U_LOG_E("Unknown active config '%s' from %s.", str, from);
		*out_active = U_ACTIVE_CONFIG_NONE;
		return false;
	}

	return true;
}

void
u_config_json_get_active(struct u_config_json *json, enum u_config_json_active_config *out_active)
{
	const char *str = debug_get_option_active_config();
	if (str != NULL && parse_active(str, "environment", out_active)) {
		return;
	}

	char tmp[256];
	if (!is_json_ok(json) || !get_obj_str(json->root, "active", tmp, sizeof(tmp))) {
		*out_active = U_ACTIVE_CONFIG_NONE;
		return;
	}

	parse_active(tmp, "json", out_active);
}

bool
u_config_json_get_remote_port(struct u_config_json *json, int *out_port)
{
	cJSON *t = cJSON_GetObjectItemCaseSensitive(json->root, "remote");
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

static cJSON *
open_tracking_settings(struct u_config_json *json)
{
	if (!is_json_ok(json)) {
		return NULL;
	}

	cJSON *t = cJSON_GetObjectItemCaseSensitive(json->root, "tracking");
	if (t == NULL) {
		U_LOG_I("Config file does not contain tracking config");
		return NULL;
	}

	return t;
}

bool
u_config_json_get_tracking_overrides(struct u_config_json *json,
                                     struct xrt_tracking_override *out_overrides,
                                     size_t *out_override_count)
{
	cJSON *t = open_tracking_settings(json);
	if (t == NULL) {
		return false;
	}


	cJSON *overrides = cJSON_GetObjectItemCaseSensitive(t, "tracking_overrides");

	*out_override_count = 0;

	cJSON *override = NULL;
	cJSON_ArrayForEach(override, overrides)
	{
		bool bad = false;

		struct xrt_tracking_override *o = &out_overrides[(*out_override_count)++];
		bad |= !get_obj_str(override, "target_device_serial", o->target_device_serial, XRT_DEVICE_NAME_LEN);
		bad |= !get_obj_str(override, "tracker_device_serial", o->tracker_device_serial, XRT_DEVICE_NAME_LEN);

		char override_type[256];
		bad |= !get_obj_str(override, "type", override_type, sizeof(override_type));
		if (strncmp(override_type, "direct", sizeof(override_type) - 1) == 0) {
			o->override_type = XRT_TRACKING_OVERRIDE_DIRECT;
		} else if (strncmp(override_type, "attached", sizeof(override_type) - 1) == 0) {
			o->override_type = XRT_TRACKING_OVERRIDE_ATTACHED;
		}

		cJSON *offset = cJSON_GetObjectItemCaseSensitive(override, "offset");
		if (offset) {
			cJSON *orientation = cJSON_GetObjectItemCaseSensitive(offset, "orientation");
			bad |= !get_obj_float(orientation, "x", &o->offset.orientation.x);
			bad |= !get_obj_float(orientation, "y", &o->offset.orientation.y);
			bad |= !get_obj_float(orientation, "z", &o->offset.orientation.z);
			bad |= !get_obj_float(orientation, "w", &o->offset.orientation.w);

			cJSON *position = cJSON_GetObjectItemCaseSensitive(offset, "position");
			bad |= !get_obj_float(position, "x", &o->offset.position.x);
			bad |= !get_obj_float(position, "y", &o->offset.position.y);
			bad |= !get_obj_float(position, "z", &o->offset.position.z);
		} else {
			o->offset.orientation.w = 1;
		}

		char input_name[512] = {'\0'};
		get_obj_str(override, "xrt_input_name", input_name, 512);
		o->input_name = xrt_input_name_enum(input_name);

		if (bad) {
			*out_override_count = 0;
			return false;
		}
	}
	return true;
}

bool
u_config_json_get_tracking_settings(struct u_config_json *json, struct xrt_settings_tracking *s)
{
	cJSON *t = open_tracking_settings(json);
	if (t == NULL) {
		return false;
	}

	char tmp[16];

	bool bad = false;

	int ver = -1;

	bad |= !get_obj_int(t, "version", &ver);
	if (bad || ver >= 1) {
		U_LOG_E("Missing or unknown version tag '%i' in tracking config", ver);
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

static void
u_config_json_make_default_root(struct u_config_json *json)
{
	json->root = cJSON_CreateObject();
}

static void
u_config_write(struct u_config_json *json, const char *filename)
{
	char *str = cJSON_Print(json->root);
	U_LOG_D("%s", str);

	FILE *config_file = u_file_open_file_in_config_dir(filename, "w");
	fprintf(config_file, "%s\n", str);
	fflush(config_file);
	fclose(config_file);
	config_file = NULL;
	free(str);
}

void
u_config_json_save_calibration(struct u_config_json *json, struct xrt_settings_tracking *settings)
{
	if (!json->file_loaded) {
		u_config_json_make_default_root(json);
	}
	u_config_json_assign_schema(json);

	cJSON *root = json->root;

	cJSON *t = cJSON_GetObjectItem(root, "tracking");
	if (!t) {
		t = cJSON_AddObjectToObject(root, "tracking");
	}

	cJSON_DeleteItemFromObject(t, "version");
	cJSON_AddNumberToObject(t, "version", 0);

	cJSON_DeleteItemFromObject(t, "camera_name");
	cJSON_AddStringToObject(t, "camera_name", settings->camera_name);

	cJSON_DeleteItemFromObject(t, "camera_mode");
	cJSON_AddNumberToObject(t, "camera_mode", settings->camera_mode);

	cJSON_DeleteItemFromObject(t, "camera_type");
	switch (settings->camera_type) {
	case XRT_SETTINGS_CAMERA_TYPE_REGULAR_MONO: cJSON_AddStringToObject(t, "camera_type", "regular_mono"); break;
	case XRT_SETTINGS_CAMERA_TYPE_REGULAR_SBS: cJSON_AddStringToObject(t, "camera_type", "regular_sbs"); break;
	case XRT_SETTINGS_CAMERA_TYPE_SLAM: cJSON_AddStringToObject(t, "camera_type", "slam_sbs"); break;
	case XRT_SETTINGS_CAMERA_TYPE_PS4: cJSON_AddStringToObject(t, "camera_type", "ps4"); break;
	case XRT_SETTINGS_CAMERA_TYPE_LEAP_MOTION: cJSON_AddStringToObject(t, "camera_type", "leap_motion"); break;
	}

	cJSON_DeleteItemFromObject(t, "calibration_path");
	cJSON_AddStringToObject(t, "calibration_path", settings->calibration_path);

	u_config_write(json, CONFIG_FILE_NAME);
}

static cJSON *
make_pose(struct xrt_pose *pose)
{
	cJSON *json = cJSON_CreateObject();

	cJSON *o = cJSON_CreateObject();
	cJSON_AddNumberToObject(o, "x", pose->orientation.x);
	cJSON_AddNumberToObject(o, "y", pose->orientation.y);
	cJSON_AddNumberToObject(o, "z", pose->orientation.z);
	cJSON_AddNumberToObject(o, "w", pose->orientation.w);
	cJSON_AddItemToObject(json, "orientation", o);

	cJSON *p = cJSON_CreateObject();
	cJSON_AddNumberToObject(p, "x", pose->position.x);
	cJSON_AddNumberToObject(p, "y", pose->position.y);
	cJSON_AddNumberToObject(p, "z", pose->position.z);
	cJSON_AddItemToObject(json, "position", p);

	return json;
}

void
u_config_json_save_overrides(struct u_config_json *json, struct xrt_tracking_override *overrides, size_t override_count)
{
	if (!json->file_loaded) {
		u_config_json_make_default_root(json);
	}
	u_config_json_assign_schema(json);
	cJSON *root = json->root;

	cJSON *t = cJSON_GetObjectItem(root, "tracking");
	if (!t) {
		t = cJSON_AddObjectToObject(root, "tracking");
	}

	cJSON_DeleteItemFromObject(t, "tracking_overrides");
	cJSON *o = cJSON_AddArrayToObject(t, "tracking_overrides");

	for (size_t i = 0; i < override_count; i++) {
		cJSON *entry = cJSON_CreateObject();

		cJSON_AddStringToObject(entry, "target_device_serial", overrides[i].target_device_serial);
		cJSON_AddStringToObject(entry, "tracker_device_serial", overrides[i].tracker_device_serial);

		char buffer[256];
		switch (overrides[i].override_type) {
		case XRT_TRACKING_OVERRIDE_DIRECT: snprintf(buffer, ARRAY_SIZE(buffer), "direct"); break;
		case XRT_TRACKING_OVERRIDE_ATTACHED: snprintf(buffer, ARRAY_SIZE(buffer), "attached"); break;
		}
		cJSON_AddStringToObject(entry, "type", buffer);

		cJSON_AddItemToObject(entry, "offset", make_pose(&overrides[i].offset));

		const char *input_name_string = xrt_input_name_string(overrides[i].input_name);
		cJSON_AddStringToObject(entry, "xrt_input_name", input_name_string);

		cJSON_AddItemToArray(o, entry);
	}

	u_config_write(json, CONFIG_FILE_NAME);
}

void
u_gui_state_open_file(struct u_config_json *json)
{
	u_config_json_open_or_create_file(json, GUI_STATE_FILE_NAME);
}

static const char *
u_gui_state_scene_to_string(enum u_gui_state_scene scene)
{
	switch (scene) {
	case GUI_STATE_SCENE_CALIBRATE: return "calibrate";
	default: assert(false); return NULL;
	}
}

struct cJSON *
u_gui_state_get_scene(struct u_config_json *json, enum u_gui_state_scene scene)
{
	if (json->root == NULL) {
		return NULL;
	}
	const char *scene_name = u_gui_state_scene_to_string(scene);

	struct cJSON *c =
	    cJSON_DetachItemFromObjectCaseSensitive(cJSON_GetObjectItemCaseSensitive(json->root, "scenes"), scene_name);
	cJSON_Delete(json->root);
	return c;
}

void
u_gui_state_save_scene(struct u_config_json *json, enum u_gui_state_scene scene, struct cJSON *new_state)
{

	if (!json->file_loaded) {
		u_config_json_make_default_root(json);
	}

	cJSON *root = json->root;

	const char *scene_name = u_gui_state_scene_to_string(scene);

	struct cJSON *sc = cJSON_GetObjectItemCaseSensitive(root, "scenes");

	if (!sc) {
		sc = cJSON_AddObjectToObject(root, "scenes");
	}
	cJSON_DeleteItemFromObject(sc, scene_name);
	cJSON_AddItemToObject(sc, scene_name, new_state);
	u_config_write(json, GUI_STATE_FILE_NAME);
}
