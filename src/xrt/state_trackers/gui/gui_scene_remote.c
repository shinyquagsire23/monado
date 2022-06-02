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

	char address[1024];
	int port;
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
handle_input(struct r_remote_controller_data *d)
{
	igText("Hover buttons and sliders to touch component.");
	bool touched = false;

	d->system_click = handle_downable_button("System");
	d->system_touch = igIsItemHovered(ImGuiHoveredFlags_RectOnly);
	igSameLine(0, 3);

	d->a_click = handle_downable_button("A");
	d->a_touch = igIsItemHovered(ImGuiHoveredFlags_RectOnly);
	igSameLine(0, 3);

	d->b_click = handle_downable_button("B");
	d->b_touch = igIsItemHovered(ImGuiHoveredFlags_RectOnly);
	igSameLine(0, 3);

	igCheckbox("Active", &d->active);

	// Squeeze
	igSliderFloat("Squeeze Value", &d->squeeze_value.x, 0, 1, "%.2f", 0);
	igSliderFloat("Squeeze Force", &d->squeeze_force.x, 0, 1, "%.2f", 0);

	// Trigger
	igText("Value > 0.0 causes touch, 0.7 > causes click");
	igSliderFloat("Trigger", &d->trigger_value.x, 0, 1, "%.2f", 0);
	touched |= igIsItemHovered(ImGuiHoveredFlags_RectOnly);
	d->trigger_click = d->trigger_value.x > 0.7;
	touched |= d->trigger_value.x > 0.0001;
	d->trigger_touch = touched;

	// Thumbstick
	touched = false;
	d->thumbstick_click = handle_downable_button("Thumbstick Click");
	touched |= igIsItemHovered(ImGuiHoveredFlags_RectOnly);
	igSliderFloat2("Thumbstick", &d->thumbstick.x, -1, 1, "%.2f", 0);
	touched |= igIsItemHovered(ImGuiHoveredFlags_RectOnly);
	d->thumbstick_touch = touched;

	// Trackpad
	touched = false;
	igSliderFloat2("Trackpad", &d->trackpad.x, -1, 1, "%.2f", 0);
	touched |= igIsItemHovered(ImGuiHoveredFlags_RectOnly);
	igSliderFloat("Trackpad Force", &d->trackpad_force.x, 0, 1, "%.2f", 0);
	touched |= igIsItemHovered(ImGuiHoveredFlags_RectOnly);
	touched |= d->trackpad_force.x >= 0.0001f;
	d->trackpad_touch = touched;
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

	if (igButton("XR_EXT_hand_tracking Touch Index Fingertips", zero_dims)) {
		d->left.pose.position.x = -0.0250000f;
		d->left.pose.position.y = 1.300000f;
		d->left.pose.position.z = -0.500000f;
		d->left.pose.orientation.x = 0.000000f;
		d->left.pose.orientation.y = 0.000000f;
		d->left.pose.orientation.z = 0.000000f;
		d->left.pose.orientation.w = 1.000000f;
		d->left.linear_velocity.x = 0.000000f;
		d->left.linear_velocity.y = 0.000000f;
		d->left.linear_velocity.z = -1.830000f;
		d->left.angular_velocity.x = -16.900000f;
		d->left.angular_velocity.y = 0.000000f;
		d->left.angular_velocity.z = 0.000000f;

		d->left.hand_curl[0] = 0.0f;
		d->left.hand_curl[1] = 0.0f;
		d->left.hand_curl[2] = 0.0f;
		d->left.hand_curl[3] = 0.0f;
		d->left.hand_curl[4] = 0.0f;


		d->right.pose.position.x = 0.0250000f;
		d->right.pose.position.y = 1.300000f;
		d->right.pose.position.z = -0.500000f;
		d->right.pose.orientation.x = 0.000000f;
		d->right.pose.orientation.y = 0.000000f;
		d->right.pose.orientation.z = 0.000000f;
		d->right.pose.orientation.w = 1.000000f;
		d->right.linear_velocity.x = 0.000000f;
		d->right.linear_velocity.y = 0.000000f;
		d->right.linear_velocity.z = -1.830000f;
		d->right.angular_velocity.x = -16.900000f;
		d->right.angular_velocity.y = 0.000000f;
		d->right.angular_velocity.z = 0.000000f;

		d->right.hand_curl[0] = 0.0f;
		d->right.hand_curl[1] = 0.0f;
		d->right.hand_curl[2] = 0.0f;
		d->right.hand_curl[3] = 0.0f;
		d->right.hand_curl[4] = 0.0f;
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
		handle_input(&d->prefix);                                                                              \
	} while (false)

#define CURL(prefix, name, index) igDragFloat(#prefix "." #name, &d->prefix.hand_curl[index], 0.01, 0.0, 1.0, "%f", 0);
#define HAND(prefix)                                                                                                   \
	do {                                                                                                           \
		igCheckbox("Hand tracking Active", &d->prefix.hand_tracking_active);                                   \
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

	igPushIDPtr(&d->hmd); // Make all IDs unique.
	POSE(hmd);
	igPopID(); // Pop unique IDs

	igPushIDPtr(&d->left); // Make all IDs unique.
	POSE(left);
	LIN_ANG(left);
	BUTTONS(left);
	HAND(left);
	igPopID(); // Pop unique IDs

	igPushIDPtr(&d->right); // Make all IDs unique.
	POSE(right);
	LIN_ANG(right);
	BUTTONS(right);
	HAND(right);
	igPopID(); // Pop unique IDs

	igCheckbox("Predefined poses", &gr->cheat_menu);
	if (gr->cheat_menu) {
		render_cheat_menu(gr, p);
	}

	r_remote_connection_write_one(&gr->rc, &gr->data);
}

static void
on_not_connected(struct gui_remote *gr, struct gui_program *p)
{
	igInputText("Address", gr->address, sizeof(gr->address), 0, NULL, NULL);
	igInputInt("Port", &gr->port, 1, 1, 0);

	bool connect = igButton("Connect", zero_dims);

	igSameLine(0, 4.0f);

	if (igButton("Exit", zero_dims)) {
		gui_scene_delete_me(p, &gr->base);
		return;
	}

	if (!connect) {
		return;
	}

	r_remote_connection_init(&gr->rc, gr->address, gr->port);
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
gui_scene_remote(struct gui_program *p, const char *address)
{
	struct gui_remote *gr = U_TYPED_CALLOC(struct gui_remote);

	gr->base.render = scene_render;
	gr->base.destroy = scene_destroy;
	gr->rc.fd = -1;

	// GUI input defaults.
	if (address != NULL) {
		snprintf(gr->address, sizeof(gr->address), "%s", address);
	} else {
		snprintf(gr->address, sizeof(gr->address), "localhost");
	}
	gr->port = 4242;

	gui_scene_push_front(p, &gr->base);
}
