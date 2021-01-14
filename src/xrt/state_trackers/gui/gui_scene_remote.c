// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Remote debugging UI.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup gui
 */

#include "xrt/xrt_config_drivers.h"

#include "util/u_misc.h"
#include "util/u_logging.h"

#include "math/m_api.h"

#include "gui_common.h"
#include "gui_imgui.h"

#include "remote/r_interface.h"


/*
 *
 * Structs and defines.
 *
 */

/*!
 * A GUI scene that lets the user select a user device.
 * @implements gui_scene
 */
struct gui_remote
{
	struct gui_scene base;

	struct r_remote_connection rc;

	struct r_remote_data reset;
	struct r_remote_data data;

	bool cheat_menu;
};

const ImVec2 zero_dims = {0, 0};


/*
 *
 * Functions.
 *
 */

#ifdef XRT_BUILD_DRIVER_REMOTE
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
handle_downable_button(const char *name)
{
	igButton(name, zero_dims);
	return igIsItemHovered(ImGuiHoveredFlags_RectOnly) && igIsMouseDown(ImGuiMouseButton_Left);
}

static void
render_cheat_menu(struct gui_remote *gr, struct gui_program *p)
{
	struct r_remote_data *d = &gr->data;

	if (igButton("Reset all", zero_dims)) {
		*d = gr->reset;
	}

	if (igButton("Interactive Throw #1", zero_dims)) {
#if 0
		d->left.pose.position.x = -1.0f;
		d->left.pose.position.y = 1.0f;
		d->left.pose.position.z = -3.0f;
#else
		d->left.pose.position.x = -0.200000f;
		d->left.pose.position.y = 1.300000f;
		d->left.pose.position.z = -0.500000f;
		d->left.pose.orientation.x = 0.000000f;
		d->left.pose.orientation.y = 0.000000f;
		d->left.pose.orientation.z = 0.000000f;
		d->left.pose.orientation.w = 1.000000f;
		d->left.linear_velocity.x = -0.770000f;
		d->left.linear_velocity.y = 3.255000f;
		d->left.linear_velocity.z = -2.620000f;
		d->left.angular_velocity.x = 0.000000f;
		d->left.angular_velocity.y = 0.000000f;
		d->left.angular_velocity.z = 0.000000f;
#endif
	}

	if (igButton("Interactive Throw #2", zero_dims)) {
#if 0
		d->left.pose.position.x = 1.0f;
		d->left.pose.position.y = 1.1f;
		d->left.pose.position.z = -4.0f;
#else
		d->left.pose.position.x = -0.200000f;
		d->left.pose.position.y = 1.300000f;
		d->left.pose.position.z = -0.500000f;
		d->left.pose.orientation.x = 0.858999f;
		d->left.pose.orientation.y = -0.163382f;
		d->left.pose.orientation.z = -0.000065f;
		d->left.pose.orientation.w = 0.485209f;
		d->left.linear_velocity.x = 0.000000f;
		d->left.linear_velocity.y = 0.000000f;
		d->left.linear_velocity.z = 0.000000f;
		d->left.angular_velocity.x = -10.625000f;
		d->left.angular_velocity.y = 0.000000f;
		d->left.angular_velocity.z = 0.000000f;
#endif
	}

	if (igButton("Interactive Throw #3", zero_dims)) {
#if 0
		d->left.pose.position.x = 0.0f;
		d->left.pose.position.y = 3.0f;
		d->left.pose.position.z = -5.0f;
#else
		d->left.pose.position.x = -0.200000f;
		d->left.pose.position.y = 1.300000f;
		d->left.pose.position.z = -0.500000f;
		d->left.pose.orientation.x = 0.862432f;
		d->left.pose.orientation.y = 0.000000f;
		d->left.pose.orientation.z = 0.000000f;
		d->left.pose.orientation.w = 0.506174f;
		d->left.linear_velocity.x = 0.000000f;
		d->left.linear_velocity.y = 0.000000f;
		d->left.linear_velocity.z = -1.830000f;
		d->left.angular_velocity.x = -16.900000f;
		d->left.angular_velocity.y = 0.000000f;
		d->left.angular_velocity.z = 0.000000f;
#endif
	}

	if (igButton("Dump left", zero_dims)) {
		U_LOG_RAW(
		    "d->left.pose.position.x = %ff;\n"
		    "d->left.pose.position.y = %ff;\n"
		    "d->left.pose.position.z = %ff;\n"
		    "d->left.pose.orientation.x = %ff;\n"
		    "d->left.pose.orientation.y = %ff;\n"
		    "d->left.pose.orientation.z = %ff;\n"
		    "d->left.pose.orientation.w = %ff;\n"
		    "d->left.linear_velocity.x = %ff;\n"
		    "d->left.linear_velocity.y = %ff;\n"
		    "d->left.linear_velocity.z = %ff;\n"
		    "d->left.angular_velocity.x = %ff;\n"
		    "d->left.angular_velocity.y = %ff;\n"
		    "d->left.angular_velocity.z = %ff;\n",
		    d->left.pose.position.x, d->left.pose.position.y, d->left.pose.position.z,
		    d->left.pose.orientation.x, d->left.pose.orientation.y, d->left.pose.orientation.z,
		    d->left.pose.orientation.w, d->left.linear_velocity.x, d->left.linear_velocity.y,
		    d->left.linear_velocity.z, d->left.angular_velocity.x, d->left.angular_velocity.y,
		    d->left.angular_velocity.z);
	}
}

#define POSE(prefix)                                                                                                   \
	do {                                                                                                           \
		handle_draggable_vec3_f32(#prefix ".pose.position", &d->prefix.pose.position,                          \
		                          &r->prefix.pose.position);                                                   \
		handle_draggable_quat(#prefix ".pose.orientation", &d->prefix.pose.orientation,                        \
		                      &r->prefix.pose.orientation);                                                    \
	} while (false)

#define LIN_ANG(prefix)                                                                                                \
	do {                                                                                                           \
		handle_draggable_vec3_f32(#prefix ".linear_velocity", &d->prefix.linear_velocity,                      \
		                          &r->prefix.linear_velocity);                                                 \
		handle_draggable_vec3_f32(#prefix ".angular_velocity", &d->prefix.angular_velocity,                    \
		                          &r->prefix.angular_velocity);                                                \
	} while (false)

#define BUTTONS(prefix)                                                                                                \
	do {                                                                                                           \
		d->prefix.select = handle_downable_button("Select");                                                   \
		igSameLine(0, 3);                                                                                      \
		d->prefix.menu = handle_downable_button("Menu");                                                       \
		igSameLine(0, 3);                                                                                      \
		igCheckbox("Active", &d->prefix.active);                                                               \
	} while (false)

#define CURL(prefix, name, index) igDragFloat(#prefix "." #name, &d->prefix.hand_curl[index], 0.01, 0.0, 1.0, "%f", 0);
#define HAND(prefix)                                                                                                   \
	do {                                                                                                           \
		CURL(prefix, little, 0);                                                                               \
		CURL(prefix, ring, 1);                                                                                 \
		CURL(prefix, middle, 2);                                                                               \
		CURL(prefix, index, 3);                                                                                \
		CURL(prefix, thumb, 4);                                                                                \
	} while (false)

static void
on_connected(struct gui_remote *gr, struct gui_program *p)
{
	const struct r_remote_data *r = &gr->reset;
	struct r_remote_data *d = &gr->data;

	const ImVec2 hmd_size = {0, 46};
	const uint32_t hand_size = 23 * 5;
	const ImVec2 ctrl_size = {0, 64 + hand_size + 52};

	igBeginChildStr("hmd", hmd_size, false, 0);
	POSE(hmd);
	igEndChild();

	igBeginChildStr("left", ctrl_size, false, 0);
	POSE(left);
	LIN_ANG(left);
	BUTTONS(left);
	HAND(left);
	igEndChild();

	igBeginChildStr("right", ctrl_size, false, 0);
	POSE(right);
	LIN_ANG(right);
	BUTTONS(right);
	HAND(right);
	igEndChild();

	igCheckbox("Predefined poses", &gr->cheat_menu);
	if (gr->cheat_menu) {
		render_cheat_menu(gr, p);
	}

	r_remote_connection_write_one(&gr->rc, &gr->data);
}

static void
on_not_connected(struct gui_remote *gr, struct gui_program *p)
{
	if (!igButton("Connect", zero_dims)) {
		return;
	}

	r_remote_connection_init(&gr->rc, "127.0.0.1", 4242);
	r_remote_connection_read_one(&gr->rc, &gr->reset);
	r_remote_connection_read_one(&gr->rc, &gr->data);
}
#endif


/*
 *
 * Scene functions.
 *
 */

static void
scene_render(struct gui_scene *scene, struct gui_program *p)
{
	struct gui_remote *gr = (struct gui_remote *)scene;
	(void)gr;

	igBegin("Remote control", NULL, 0);

#ifdef XRT_BUILD_DRIVER_REMOTE
	if (gr->rc.fd < 0) {
		on_not_connected(gr, p);
	} else {
		on_connected(gr, p);
	}
#else
	igText("Not compiled with the remote driver");
	if (igButton("Exit", zero_dims)) {
		gui_scene_delete_me(p, &gr->base);
	}
#endif

	igEnd();
}

static void
scene_destroy(struct gui_scene *scene, struct gui_program *p)
{
	struct gui_remote *gr = (struct gui_remote *)scene;
	(void)gr;

	free(scene);
}


/*
 *
 * 'Exported' functions.
 *
 */

void
gui_scene_remote(struct gui_program *p)
{
	struct gui_remote *gr = U_TYPED_CALLOC(struct gui_remote);

	gr->base.render = scene_render;
	gr->base.destroy = scene_destroy;
	gr->rc.fd = -1;

	gui_scene_push_front(p, &gr->base);
}
