// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  A very small scene that lets the user configure tracking overrides.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup gui
 */

#include "util/u_misc.h"
#include "util/u_format.h"
#include "util/u_logging.h"

#include "util/u_config_json.h"

#include "math/m_api.h"

#include "xrt/xrt_prober.h"
#include "xrt/xrt_settings.h"
#include "xrt/xrt_system.h"

#include "gui_common.h"
#include "gui_imgui.h"

#include "bindings/b_generated_bindings.h"

struct gui_tracking_overrides
{
	struct gui_scene base;

	int gui_edit_override_index;

	bool gui_add_override_active;
	int add_target;
	int add_tracker;

	struct u_config_json config;

	struct xrt_pose reset_offset;

	size_t num_overrides;
	struct xrt_tracking_override overrides[XRT_MAX_TRACKING_OVERRIDES];
};

static char *override_type_str[2] = {
    [XRT_TRACKING_OVERRIDE_DIRECT] = "direct",
    [XRT_TRACKING_OVERRIDE_ATTACHED] = "attached",
};

static ImVec2 button_dims = {256 + 64, 0};

/*
 *
 * Internal functions.
 *
 */

#define NAME_LENGTH XRT_DEVICE_NAME_LEN * 2 + 5

static void
make_name(struct xrt_device *xdev, char *buf)
{
	snprintf(buf, NAME_LENGTH, "%s | %s", xdev->str, xdev->serial);
}

static void
handle_draggable_vec3_f32(const char *name, struct xrt_vec3 *v, const struct xrt_vec3 *reset)
{
	float min = -256.0f;
	float max = 256.0f;
	char tmp[256];

	snprintf(tmp, sizeof(tmp), "%s.reset", name);

	if (igArrowButton(tmp, ImGuiDir_Left)) {
		*v = *reset;
	}

	igSameLine(0, 3);
	igDragFloat3(name, (float *)v, 0.005f, min, max, "%+f", 1.0f);
}

static void
handle_draggable_quat(const char *name, struct xrt_quat *q, const struct xrt_quat *reset)
{
	float min = -1.0f;
	float max = 1.0f;

	char tmp[256];

	snprintf(tmp, sizeof(tmp), "%s.reset", name);

	if (igArrowButton(tmp, ImGuiDir_Left)) {
		*q = *reset;
	}

	igSameLine(0, 3);
	igDragFloat4(name, (float *)q, 0.005f, min, max, "%+f", 1.0f);

	// Avoid invalid
	if (q->x == 0.0f && q->y == 0.0f && q->z == 0.0f && q->w == 0.0f) {
		q->w = 1.0f;
	}

	// And make sure it's a unit rotation.
	math_quat_normalize(q);
}

static bool
get_indices(struct gui_program *p,
            struct gui_tracking_overrides *ts,
            struct xrt_tracking_override *override,
            int *out_target,
            int *out_tracker)
{
	bool has_target = false;
	bool has_tracker = false;

	for (int i = 0; i < XRT_SYSTEM_MAX_DEVICES; i++) {
		if (!p->xsysd->xdevs[i]) {
			continue;
		}

		if (strcmp(p->xsysd->xdevs[i]->serial, override->target_device_serial) == 0) {
			has_target = true;
			*out_target = i;
		}

		if (strcmp(p->xsysd->xdevs[i]->serial, override->tracker_device_serial) == 0) {
			has_tracker = true;
			*out_tracker = i;
		}
	}

	return has_tracker && has_target;
}

static struct xrt_pose identity = {.position = {.x = 0, .y = 0, .z = 0},
                                   .orientation = {.x = 0, .y = 0, .z = 0, .w = 1}};

static void
gui_add_override(struct gui_program *p, struct gui_tracking_overrides *ts)
{
	igBegin("Target Device", NULL, 0);
	for (int i = 0; i < 8; i++) {
		if (!p->xsysd->xdevs[i]) {
			continue;
		}

		char buf[NAME_LENGTH];
		make_name(p->xsysd->xdevs[i], buf);

		bool selected = ts->add_target == i;
		if (igCheckbox(buf, &selected)) {
			ts->add_target = i;
		}
	}
	igEnd();

	igBegin("Tracker Device", NULL, 0);
	for (int i = 0; i < 8; i++) {
		if (!p->xsysd->xdevs[i]) {
			continue;
		}

		char buf[NAME_LENGTH];
		make_name(p->xsysd->xdevs[i], buf);

		bool selected = ts->add_tracker == i;
		if (igCheckbox(buf, &selected)) {
			ts->add_tracker = i;
		}
	}
	igEnd();

	if (ts->add_target >= 0 && ts->add_tracker >= 0 && ts->add_target != ts->add_tracker) {
		struct xrt_tracking_override *o = &ts->overrides[ts->num_overrides];
		strncpy(o->target_device_serial, p->xsysd->xdevs[ts->add_target]->serial, XRT_DEVICE_NAME_LEN);
		strncpy(o->tracker_device_serial, p->xsysd->xdevs[ts->add_tracker]->serial, XRT_DEVICE_NAME_LEN);
		o->offset = identity;

		// set input_name to the first pose in the inputs
		for (uint32_t i = 0; i < p->xsysd->xdevs[ts->add_tracker]->input_count; i++) {
			enum xrt_input_name input_name = p->xsysd->xdevs[ts->add_tracker]->inputs[i].name;
			if (XRT_GET_INPUT_TYPE(input_name) != XRT_INPUT_TYPE_POSE) {
				continue;
			}
			o->input_name = input_name;
			break;
		}

		ts->num_overrides += 1;

		ts->add_target = -1;
		ts->add_tracker = -1;

		ts->gui_add_override_active = false;

		// immediately open for editing
		ts->gui_edit_override_index = ts->num_overrides - 1;
	}
}

static void
scene_render(struct gui_scene *scene, struct gui_program *p)
{
	struct gui_tracking_overrides *ts = (struct gui_tracking_overrides *)scene;

	// don't edit and add at the same time
	if (ts->gui_add_override_active) {
		ts->gui_edit_override_index = -1;
	}

	if (ts->gui_edit_override_index >= 0) {
		struct xrt_tracking_override *o = &ts->overrides[ts->gui_edit_override_index];

		igBegin("Tracker Device Offset", NULL, 0);
		int target = -1;
		int tracker = -1;
		if (get_indices(p, ts, o, &target, &tracker)) {
			igText("Editing %s [%s] <- %s [%s]", p->xsysd->xdevs[target]->str, o->target_device_serial,
			       p->xsysd->xdevs[tracker]->str, o->tracker_device_serial);
		} else {
			igText("Editing unconnected %s <- %s", o->target_device_serial, o->tracker_device_serial);
		}
		handle_draggable_vec3_f32("Position", &o->offset.position, &ts->reset_offset.position);
		handle_draggable_quat("Orientation", &o->offset.orientation, &ts->reset_offset.orientation);

		igText("Tracking Override Type");
		for (int i = 0; i < 2; i++) {
			bool selected = (int)o->override_type == i;
			if (igCheckbox(override_type_str[i], &selected)) {
				o->override_type = (enum xrt_tracking_override_type)i;
			}
		}

		if (tracker >= 0) {
			igText("Tracker Input Pose Name");
			for (uint32_t i = 0; i < p->xsysd->xdevs[tracker]->input_count; i++) {
				enum xrt_input_name input_name = p->xsysd->xdevs[tracker]->inputs[i].name;
				if (XRT_GET_INPUT_TYPE(input_name) != XRT_INPUT_TYPE_POSE) {
					continue;
				}

				const char *name_str = xrt_input_name_string(input_name);
				bool selected = o->input_name == input_name;
				if (igCheckbox(name_str, &selected)) {
					o->input_name = input_name;
				}
			}
		}
		igEnd();
	}

	if (ts->gui_add_override_active) {
		gui_add_override(p, ts);
	}

	igBegin("Tracking Overrides", NULL, 0);

	igText("Existing Overrides");
	for (size_t i = 0; i < ts->num_overrides; i++) {
		// make the delete buttons work
		igPushIDInt(i);

		igSeparator();

		bool checked = ts->gui_edit_override_index == (int)i;

		char buf[XRT_DEVICE_NAME_LEN * 2 + 10];
		snprintf(buf, sizeof(buf), "%s <- %s", ts->overrides[i].target_device_serial,
		         ts->overrides[i].tracker_device_serial);
		if (igCheckbox(buf, &checked)) {

			// skip adding override when clicking to edit one
			ts->gui_add_override_active = false;

			ts->gui_edit_override_index = i;
			ts->reset_offset = ts->overrides[i].offset;
		}
		if (igButton("Delete this override", button_dims)) {
			for (size_t j = i; j < ts->num_overrides - 1; j++) {
				ts->overrides[j] = ts->overrides[j + 1];
			}
			ts->num_overrides--;
			if (ts->gui_edit_override_index >= (int)i) {
				ts->gui_edit_override_index -= 1;
			}
		}

		igSeparator();

		igPopID();
	}

	igSeparator();

	if (igButton("Add one", button_dims)) {
		if (ts->num_overrides < XRT_MAX_TRACKING_OVERRIDES) {
			ts->gui_add_override_active = true;
		}
	}

	igSeparator();

	if (igButton("Save", button_dims)) {
		u_config_json_save_overrides(&ts->config, ts->overrides, ts->num_overrides);
		u_config_json_close(&ts->config);
		gui_scene_delete_me(p, scene);
	}

	if (igButton("Exit", button_dims)) {
		gui_scene_delete_me(p, scene);
	}

	igEnd();
}

static void
scene_destroy(struct gui_scene *scene, struct gui_program *p)
{
	// 	struct tracking_overrides *ts = (struct tracking_overrides *)scene;

	free(scene);
}

static struct gui_tracking_overrides *
create(struct gui_program *p)
{
	struct gui_tracking_overrides *ts = U_TYPED_CALLOC(struct gui_tracking_overrides);

	ts->base.render = scene_render;
	ts->base.destroy = scene_destroy;

	ts->gui_edit_override_index = -1;
	ts->gui_add_override_active = false;
	ts->add_target = -1;
	ts->add_tracker = -1;

	u_config_json_open_or_create_main_file(&ts->config);
	u_config_json_get_tracking_overrides(&ts->config, ts->overrides, &ts->num_overrides);

	return ts;
}


/*
 *
 * 'Exported' functions.
 *
 */

void
gui_scene_tracking_overrides(struct gui_program *p)
{
	struct gui_tracking_overrides *ts = create(p);

	gui_prober_select(p);

	gui_scene_push_front(p, &ts->base);
}
