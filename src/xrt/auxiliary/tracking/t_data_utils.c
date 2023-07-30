// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Small data helpers for calibration.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_tracking
 */

#include "tracking/t_tracking.h"

#include "util/u_config_json.h"
#include "util/u_misc.h"
#include "util/u_logging.h"
#include "util/u_pretty_print.h"

#include <stdio.h>
#include <assert.h>


/*
 *
 * Helpers
 *
 */

#define P(...)                                                                                                         \
	do {                                                                                                           \
		ssize_t _ret = 0;                                                                                      \
		if ((size_t)curr < sizeof(buf)) {                                                                      \
			_ret = snprintf(buf + curr, sizeof(buf) - (size_t)curr, __VA_ARGS__);                          \
		}                                                                                                      \
		if (_ret > 0) {                                                                                        \
			curr += _ret;                                                                                  \
		}                                                                                                      \
	} while (false)

static void
dump_mat(const char *var, double mat[3][3])
{
	char buf[1024];
	ssize_t curr = 0;

	P("%s = [\n", var);
	for (uint32_t row = 0; row < 3; row++) {
		P("\t");
		for (uint32_t col = 0; col < 3; col++) {
			P("%f", mat[row][col]);
			if (col < 2) {
				P(", ");
			}
		}
		P("\n");
	}
	P("\t]");

	U_LOG_RAW("%s", buf);
}

static void
dump_vector(const char *var, double vec[3])
{
	char buf[1024];
	ssize_t curr = 0;

	P("%s = [", var);
	for (uint32_t col = 0; col < 3; col++) {
		P("%f", vec[col]);
		if (col < 2) {
			P(", ");
		}
	}
	P("]");

	U_LOG_RAW("%s", buf);
}

static void
dump_size(const char *var, struct xrt_size size)
{
	U_LOG_RAW("%s = [%ux%u]", var, size.w, size.h);
}

static void
dump_distortion(struct t_camera_calibration *view)
{
	char buf[1024];
	ssize_t curr = 0;
	U_LOG_RAW("distortion_model = %s", t_stringify_camera_distortion_model(view->distortion_model));

	P("distortion = [");
	size_t num = t_num_params_from_distortion_model(view->distortion_model);
	for (uint32_t col = 0; col < num; col++) {
		P("%f", view->distortion_parameters_as_array[col]);
		if (col < num - 1) {
			P(", ");
		}
	}
	P("]");

	U_LOG_RAW("%s", buf);
}


/*
 *
 * 'Exported' functions.
 *
 */

void
t_stereo_camera_calibration_alloc(struct t_stereo_camera_calibration **out_c,
                                  const enum t_camera_distortion_model distortion_model)
{
	struct t_stereo_camera_calibration *c = U_TYPED_CALLOC(struct t_stereo_camera_calibration);
	c->view[0].distortion_model = distortion_model;
	c->view[1].distortion_model = distortion_model;
	t_stereo_camera_calibration_reference(out_c, c);
}

void
t_stereo_camera_calibration_destroy(struct t_stereo_camera_calibration *c)
{
	free(c);
}

void
t_camera_calibration_dump(struct t_camera_calibration *c)
{
	U_LOG_RAW("t_camera_calibration {");
	dump_size("image_size_pixels", c->image_size_pixels);
	dump_mat("intrinsic", c->intrinsics);
	dump_distortion(c);
	U_LOG_RAW("}");
}

void
t_stereo_camera_calibration_dump(struct t_stereo_camera_calibration *c)
{
	U_LOG_RAW("t_stereo_camera_calibration {");
	U_LOG_RAW("view[0] = ");
	t_camera_calibration_dump(&c->view[0]);
	U_LOG_RAW("view[1] = ");
	t_camera_calibration_dump(&c->view[1]);
	dump_vector("camera_translation", c->camera_translation);
	dump_mat("camera_rotation", c->camera_rotation);
	U_LOG_RAW("}");
}

void
t_calibration_gui_params_parse_from_json(const struct cJSON *params, struct t_calibration_params *p)
{
	if (params == NULL || p == NULL) {
		return;
	}

	const struct cJSON *c = params; // Less typing


	u_json_get_bool(u_json_get(c, "use_fisheye"), &p->use_fisheye);
	u_json_get_bool(u_json_get(c, "stereo_sbs"), &p->stereo_sbs);
	u_json_get_bool(u_json_get(c, "save_images"), &p->save_images);
	u_json_get_bool(u_json_get(c, "mirror_rgb_image"), &p->mirror_rgb_image);


	u_json_get_int(u_json_get(c, "num_cooldown_frames"), &p->num_cooldown_frames);
	u_json_get_int(u_json_get(c, "num_wait_for"), &p->num_wait_for);
	u_json_get_int(u_json_get(c, "num_collect_total"), &p->num_collect_total);
	u_json_get_int(u_json_get(c, "num_collect_restart"), &p->num_collect_restart);

	u_json_get_bool(u_json_get(u_json_get(c, "load"), "enabled"), &p->load.enabled);
	u_json_get_int(u_json_get(u_json_get(c, "load"), "num_images"), &p->load.num_images);


	const struct cJSON *pattern_j = u_json_get(c, "pattern");
	char *pattern_s = cJSON_GetStringValue(pattern_j);
	if (pattern_s != NULL) {
		if (strcmp(pattern_s, "checkers") == 0) {
			p->pattern = T_BOARD_CHECKERS;
		} else if (strcmp(pattern_s, "sb_checkers") == 0) {
			p->pattern = T_BOARD_SB_CHECKERS;
		} else if (strcmp(pattern_s, "circles") == 0) {
			p->pattern = T_BOARD_CIRCLES;
		} else if (strcmp(pattern_s, "asymmetric_circles") == 0) {
			p->pattern = T_BOARD_ASYMMETRIC_CIRCLES;
		}
		// If we get here that is bad
	}

	{
		const struct cJSON *pat = u_json_get(c, "checkers");
		u_json_get_int(u_json_get(pat, "cols"), &p->checkers.cols);
		u_json_get_int(u_json_get(pat, "rows"), &p->checkers.rows);
		u_json_get_float(u_json_get(pat, "size_meters"), &p->checkers.size_meters);
		u_json_get_bool(u_json_get(pat, "subpixel_enable"), &p->checkers.subpixel_enable);
		u_json_get_int(u_json_get(pat, "subpixel_size"), &p->checkers.subpixel_size);
	}
	{
		const struct cJSON *pat = u_json_get(c, "sb_checkers");
		u_json_get_int(u_json_get(pat, "cols"), &p->sb_checkers.cols);
		u_json_get_int(u_json_get(pat, "rows"), &p->sb_checkers.rows);
		u_json_get_float(u_json_get(pat, "size_meters"), &p->sb_checkers.size_meters);
		u_json_get_bool(u_json_get(pat, "marker"), &p->sb_checkers.marker);
		u_json_get_bool(u_json_get(pat, "normalize_image"), &p->sb_checkers.normalize_image);
	}
	{
		const struct cJSON *pat = u_json_get(c, "circles");
		u_json_get_int(u_json_get(pat, "cols"), &p->circles.cols);
		u_json_get_int(u_json_get(pat, "rows"), &p->circles.rows);
		u_json_get_float(u_json_get(pat, "distance_meters"), &p->circles.distance_meters);
	}
	{
		const struct cJSON *pat = u_json_get(c, "asymmetric_circles");
		u_json_get_int(u_json_get(pat, "cols"), &p->asymmetric_circles.cols);
		u_json_get_int(u_json_get(pat, "rows"), &p->asymmetric_circles.rows);
		u_json_get_float(u_json_get(pat, "diagonal_distance_meters"),
		                 &p->asymmetric_circles.diagonal_distance_meters);
	}
}

void
t_calibration_gui_params_to_json(struct cJSON **out_json, struct t_calibration_params *p)
{
	struct cJSON *scene = cJSON_CreateObject();

	cJSON_AddBoolToObject(scene, "use_fisheye", p->use_fisheye);
	cJSON_AddBoolToObject(scene, "stereo_sbs", p->stereo_sbs);

	cJSON_AddBoolToObject(scene, "mirror_rgb_image", p->mirror_rgb_image);
	cJSON_AddBoolToObject(scene, "save_images", p->save_images);

	cJSON_AddNumberToObject(scene, "num_cooldown_frames", p->num_cooldown_frames);
	cJSON_AddNumberToObject(scene, "num_wait_for", p->num_wait_for);
	cJSON_AddNumberToObject(scene, "num_collect_total", p->num_collect_total);
	cJSON_AddNumberToObject(scene, "num_collect_restart", p->num_collect_restart);

	struct cJSON *load = cJSON_AddObjectToObject(scene, "load");
	cJSON_AddBoolToObject(load, "enabled", p->load.enabled);
	cJSON_AddNumberToObject(load, "num_images", p->load.num_images);

	switch (p->pattern) {
	case T_BOARD_CHECKERS: cJSON_AddStringToObject(scene, "pattern", "checkers"); break;
	case T_BOARD_SB_CHECKERS: cJSON_AddStringToObject(scene, "pattern", "sb_checkers"); break;
	case T_BOARD_CIRCLES: cJSON_AddStringToObject(scene, "pattern", "circles"); break;
	case T_BOARD_ASYMMETRIC_CIRCLES: cJSON_AddStringToObject(scene, "pattern", "asymmetric_circles"); break;
	}

	{
		struct cJSON *pat = cJSON_AddObjectToObject(scene, "checkers");
		cJSON_AddNumberToObject(pat, "cols", p->checkers.cols);
		cJSON_AddNumberToObject(pat, "rows", p->checkers.rows);
		cJSON_AddNumberToObject(pat, "size_meters", p->checkers.size_meters);
		cJSON_AddBoolToObject(pat, "subpixel_enable", p->checkers.subpixel_enable);
		cJSON_AddNumberToObject(pat, "subpixel_size", p->checkers.subpixel_size);
	}

	{
		struct cJSON *pat = cJSON_AddObjectToObject(scene, "sb_checkers");
		cJSON_AddNumberToObject(pat, "cols", p->sb_checkers.cols);
		cJSON_AddNumberToObject(pat, "rows", p->sb_checkers.rows);
		cJSON_AddNumberToObject(pat, "size_meters", p->sb_checkers.size_meters);
		cJSON_AddBoolToObject(pat, "marker", p->sb_checkers.marker);
		cJSON_AddBoolToObject(pat, "normalize_image", p->sb_checkers.normalize_image);
	}

	{
		struct cJSON *pat = cJSON_AddObjectToObject(scene, "circles");
		cJSON_AddNumberToObject(pat, "cols", p->circles.cols);
		cJSON_AddNumberToObject(pat, "rows", p->circles.rows);
		cJSON_AddNumberToObject(pat, "distance_meters", p->circles.distance_meters);
	}

	{
		struct cJSON *pat = cJSON_AddObjectToObject(scene, "asymmetric_circles");
		cJSON_AddNumberToObject(pat, "cols", p->asymmetric_circles.cols);
		cJSON_AddNumberToObject(pat, "rows", p->asymmetric_circles.rows);
		cJSON_AddNumberToObject(pat, "diagonal_distance_meters",
		                        p->asymmetric_circles.diagonal_distance_meters);
	}
	*out_json = scene;
}

void
t_calibration_gui_params_default(struct t_calibration_params *p)
{
	// Camera config.
	p->use_fisheye = false;
	p->stereo_sbs = true;

	// Which board should we calibrate against.
	p->pattern = T_BOARD_CHECKERS;

	// Checker board.
	p->checkers.cols = 9;
	p->checkers.rows = 7;
	p->checkers.size_meters = 0.025f;
	p->checkers.subpixel_enable = true;
	p->checkers.subpixel_size = 5;

	// Sector based checker board.
	p->sb_checkers.cols = 14;
	p->sb_checkers.rows = 9;
	p->sb_checkers.size_meters = 0.01206f;
	p->sb_checkers.marker = false;
	p->sb_checkers.normalize_image = false;

	// Symmetrical circles.
	p->circles.cols = 9;
	p->circles.rows = 7;
	p->circles.distance_meters = 0.025f;

	// Asymmetrical circles.
	p->asymmetric_circles.cols = 5;
	p->asymmetric_circles.rows = 17;
	p->asymmetric_circles.diagonal_distance_meters = 0.02f;

	// Loading of images.
	p->load.enabled = false;
	p->load.num_images = 20;

	// Frame collection info.
	p->num_cooldown_frames = 20;
	p->num_wait_for = 5;
	p->num_collect_total = 20;
	p->num_collect_restart = 1;

	// Misc.
	p->mirror_rgb_image = false;
	p->save_images = true;
}

void
t_calibration_gui_params_load_or_default(struct t_calibration_params *p)
{
	t_calibration_gui_params_default(p);

	// Load defaults from file, if it exists. This overwrites the preceding
	struct u_config_json config_json = {0};

	u_gui_state_open_file(&config_json);

	if (config_json.root != NULL) {
		struct cJSON *scene = u_gui_state_get_scene(&config_json, GUI_STATE_SCENE_CALIBRATE);
		t_calibration_gui_params_parse_from_json(scene, p);
	}
}

static void
t_inertial_calibration_dump_pp(u_pp_delegate_t dg, struct t_inertial_calibration *c)
{
	u_pp(dg, "t_inertial_calibration {");
	u_pp_array2d_f64(dg, &c->transform[0][0], 3, 3, "transform", "\t");
	u_pp_array_f64(dg, c->offset, 3, "offset", "\t");
	u_pp_array_f64(dg, c->bias_std, 3, "bias_std", "\t");
	u_pp_array_f64(dg, c->noise_std, 3, "noise_std", "\t");
	u_pp(dg, "}");
}

void
t_inertial_calibration_dump(struct t_inertial_calibration *c)
{
	struct u_pp_sink_stack_only sink;
	u_pp_delegate_t dg = u_pp_sink_stack_only_init(&sink);
	t_inertial_calibration_dump_pp(dg, c);
	U_LOG(U_LOGGING_DEBUG, "%s", sink.buffer);
}

void
t_imu_calibration_dump(struct t_imu_calibration *c)
{
	struct u_pp_sink_stack_only sink;
	u_pp_delegate_t dg = u_pp_sink_stack_only_init(&sink);

	u_pp(dg, "t_imu_calibration {\n");
	u_pp(dg, "accel = ");
	t_inertial_calibration_dump_pp(dg, &c->accel);
	u_pp(dg, "gyro = ");
	t_inertial_calibration_dump_pp(dg, &c->gyro);
	u_pp(dg, "}");

	U_LOG(U_LOGGING_DEBUG, "%s", sink.buffer);
}
