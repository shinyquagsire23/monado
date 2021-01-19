// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Calibration gui scene.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup gui
 */

#include "xrt/xrt_config_have.h"
#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_sink.h"
#include "util/u_file.h"
#include "util/u_json.h"

#ifdef XRT_HAVE_OPENCV
#include "tracking/t_tracking.h"
#endif

#include "xrt/xrt_frame.h"
#include "xrt/xrt_prober.h"
#include "xrt/xrt_settings.h"
#include "xrt/xrt_tracking.h"
#include "xrt/xrt_frameserver.h"

#include "gui_common.h"
#include "gui_imgui.h"

#include <assert.h>

/*!
 * An OpenCV-based camera calibration scene.
 * @implements gui_scene
 */
struct calibration_scene
{
	struct gui_scene base;

#ifdef XRT_HAVE_OPENCV
	struct t_calibration_params params;
	struct t_calibration_status status;
#endif

	struct xrt_frame_context *xfctx;
	struct xrt_fs *xfs;
	struct xrt_settings_tracking *settings;

	char filename[16];

	bool saved;
};


/*
 *
 * Internal functions.
 *
 */

#ifdef XRT_HAVE_OPENCV
static void
saved_header(struct calibration_scene *cs)
{
	if (cs->saved) {
		igText("Saved!");
	} else {
		igText("#### NOT SAVED! NOT SAVED! NOT SAVED! NOT SAVED! ####");
	}
}

static void
save_calibration(struct calibration_scene *cs)
{
	igText("Calibration complete - showing preview of undistortion.");

	saved_header(cs);
	igSetNextItemWidth(115);
	igInputText(".calibration", cs->filename, sizeof(cs->filename), 0, NULL, NULL);
	igSameLine(0.0f, 4.0f);

	static ImVec2 button_dims = {0, 0};
	if (!igButton("Save", button_dims)) {
		saved_header(cs);
		return;
	}


	/*
	 *
	 * Create the calibration path.
	 *
	 */

	char tmp[sizeof(cs->filename) + 16];
	snprintf(tmp, sizeof(tmp), "%s.calibration", cs->filename);

	u_file_get_path_in_config_dir(tmp, cs->settings->calibration_path, sizeof(cs->settings->calibration_path));

	/*
	 *
	 * Camera config file.
	 *
	 */

	cJSON *root = cJSON_CreateObject();
	cJSON *t = cJSON_AddObjectToObject(root, "tracking");
	cJSON_AddNumberToObject(t, "version", 0);
	cJSON_AddStringToObject(t, "camera_name", cs->settings->camera_name);
	cJSON_AddNumberToObject(t, "camera_mode", cs->settings->camera_mode);
	switch (cs->settings->camera_type) {
	case XRT_SETTINGS_CAMERA_TYPE_REGULAR_MONO: cJSON_AddStringToObject(t, "camera_type", "regular_mono"); break;
	case XRT_SETTINGS_CAMERA_TYPE_REGULAR_SBS: cJSON_AddStringToObject(t, "camera_type", "regular_sbs"); break;
	case XRT_SETTINGS_CAMERA_TYPE_PS4: cJSON_AddStringToObject(t, "camera_type", "ps4"); break;
	case XRT_SETTINGS_CAMERA_TYPE_LEAP_MOTION: cJSON_AddStringToObject(t, "camera_type", "leap_motion"); break;
	}
	cJSON_AddStringToObject(t, "calibration_path", cs->settings->calibration_path);

	char *str = cJSON_Print(root);
	U_LOG_D("%s", str);
	cJSON_Delete(root);

	FILE *config_file = u_file_open_file_in_config_dir("config_v0.json", "w");
	fprintf(config_file, "%s\n", str);
	fflush(config_file);
	fclose(config_file);
	config_file = NULL;
	free(str);


	/*
	 *
	 * Camera calibration file.
	 *
	 */

	FILE *calib_file = fopen(cs->settings->calibration_path, "wb");
	t_stereo_camera_calibration_save_v1(calib_file, cs->status.stereo_data);
	fclose(calib_file);
	calib_file = NULL;

	cs->saved = true;
}
#endif

static void
draw_texture(struct gui_ogl_texture *tex, bool header)
{
	if (tex == NULL) {
		return;
	}

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
	if (header && !igCollapsingHeaderBoolPtr(tex->name, NULL, flags)) {
		return;
	}

	gui_ogl_sink_update(tex);

	int w = tex->w / (tex->half ? 2 : 1);
	int h = tex->h / (tex->half ? 2 : 1);

	ImVec2 size = {(float)w, (float)h};
	ImVec2 uv0 = {0, 0};
	ImVec2 uv1 = {1, 1};
	ImVec4 white = {1, 1, 1, 1};
	ImTextureID id = (ImTextureID)(intptr_t)tex->id;
	igImage(id, size, uv0, uv1, white, white);
	igText("Sequence %u", (uint32_t)tex->seq);

	char temp[512];
	snprintf(temp, 512, "Half (%s)", tex->name);
	igCheckbox(temp, &tex->half);
}

static void
render_progress(struct calibration_scene *cs)
{
#ifdef XRT_HAVE_OPENCV
	if (cs->status.finished) {
		save_calibration(cs);
		return;
	}

	static const ImVec2 progress_dims = {150, 0};
	if (cs->status.cooldown > 0) {
		// This progress bar intentionally counts down to 0.
		float cooldown = (float)(cs->status.cooldown) / (float)cs->params.num_cooldown_frames;
		igText("Move to a new position");
		igProgressBar(cooldown, progress_dims, "Move to new position");
	} else if (!cs->status.found) {
		// This progress bar is always zero:
		// comes before "hold still"
		igText("Show board");
		igProgressBar(0.0f, progress_dims, "Show board");

	} else {
		// This progress bar counts up from zero before
		// capturing.
		int waits_complete = cs->params.num_wait_for - cs->status.waits_remaining;
		float hold_completion = (float)waits_complete / (float)cs->params.num_wait_for;
		if (cs->status.waits_remaining == 0) {
			igText("Capturing and processing!");
		} else {
			igText("Hold still! (%i/%i)", waits_complete, cs->params.num_wait_for);
		}
		igProgressBar(hold_completion, progress_dims, "Hold still!");
	}
	float capture_completion = ((float)cs->status.num_collected) / (float)cs->params.num_collect_total;
	igText("Overall progress: %i of %i frames captured", cs->status.num_collected, cs->params.num_collect_total);
	igProgressBar(capture_completion, progress_dims, NULL);

#else
	// Unused
	(void)cs;
#endif // XRT_HAVE_OPENCV
}

XRT_MAYBE_UNUSED static void
scene_render_video(struct gui_scene *scene, struct gui_program *p)
{
	struct calibration_scene *cs = (struct calibration_scene *)scene;

	igBegin("Calibration", NULL, 0);

	// Manipulated textures
	draw_texture(p->texs[0], false);

	// Progress widgets
	render_progress(cs);

	// Raw textures
	draw_texture(p->texs[1], true);

	for (size_t i = 2; i < ARRAY_SIZE(p->texs); i++) {
		draw_texture(p->texs[i], true);
	}

	igSeparator();

	static ImVec2 button_dims = {0, 0};
	if (igButton("Exit", button_dims)) {
		gui_scene_delete_me(p, &cs->base);
	}

	igEnd();
}

static void
scene_render_select(struct gui_scene *scene, struct gui_program *p)
{
	struct calibration_scene *cs = (struct calibration_scene *)scene;

#ifdef XRT_HAVE_OPENCV
	igBegin("Params", NULL, 0);

	igComboStr("Type", (int *)&cs->settings->camera_type,
	           "Regular Mono\0Regular Stereo (Side-by-Side)\0PS4\0Leap Motion Controller\0\0", -1);

	switch (cs->settings->camera_type) {
	case XRT_SETTINGS_CAMERA_TYPE_REGULAR_MONO:
		igCheckbox("Fisheye Camera", &cs->params.use_fisheye);
		cs->params.stereo_sbs = false;
		break;
	case XRT_SETTINGS_CAMERA_TYPE_REGULAR_SBS:
		igCheckbox("Fisheye Camera", &cs->params.use_fisheye);
		cs->params.stereo_sbs = true;
		break;
	case XRT_SETTINGS_CAMERA_TYPE_PS4:
		cs->params.use_fisheye = false;
		cs->params.stereo_sbs = true;
		break;
	case XRT_SETTINGS_CAMERA_TYPE_LEAP_MOTION:
		cs->params.use_fisheye = true;
		cs->params.stereo_sbs = true;
		break;
	}

	igSeparator();
	igCheckbox("Mirror on-screen preview", &cs->params.mirror_rgb_image);
	igCheckbox("Save images", &cs->params.save_images);

	igSeparator();
	igCheckbox("Load images", &cs->params.load.enabled);
	if (cs->params.load.enabled) {
		igInputInt("# images", &cs->params.load.num_images, 1, 5, 0);
	}

	igSeparator();
	igInputInt("Cooldown for # frames", &cs->params.num_cooldown_frames, 1, 5, 0);
	igInputInt("Wait for # frames (steady)", &cs->params.num_wait_for, 1, 5, 0);
	igInputInt("Collect # measurements", &cs->params.num_collect_total, 1, 5, 0);
	igInputInt("Collect in groups of #", &cs->params.num_collect_restart, 1, 5, 0);

	igSeparator();
	igComboStr("Board type", (int *)&cs->params.pattern, "Checkers\0Circles\0Asymetric Circles\0\0", 3);
	switch (cs->params.pattern) {
	case T_BOARD_CHECKERS:
		igInputInt("Checkerboard Rows", &cs->params.checkers.rows, 1, 5, 0);
		igInputInt("Checkerboard Columns", &cs->params.checkers.cols, 1, 5, 0);
		igInputFloat("Checker Size (m)", &cs->params.checkers.size_meters, 0.0005, 0.001, NULL, 0);
		igCheckbox("Subpixel", &cs->params.checkers.subpixel_enable);
		igInputInt("Subpixel Search Size", &cs->params.checkers.subpixel_size, 1, 5, 0);
		break;
	case T_BOARD_CIRCLES:
		igInputInt("Circle Rows", &cs->params.circles.rows, 1, 5, 0);
		igInputInt("Circle Columns", &cs->params.circles.cols, 1, 5, 0);
		igInputFloat("Spacing (m)", &cs->params.circles.distance_meters, 0.0005, 0.001, NULL, 0);
		break;
	case T_BOARD_ASYMMETRIC_CIRCLES:
		igInputInt("Circle Rows", &cs->params.asymmetric_circles.rows, 1, 5, 0);
		igInputInt("Circle Columns", &cs->params.asymmetric_circles.cols, 1, 5, 0);
		igInputFloat("Diagonal spacing (m)", &cs->params.asymmetric_circles.diagonal_distance_meters, 0.0005,
		             0.001, NULL, 0);
		break;
	default: assert(false);
	}

	static ImVec2 button_dims = {0, 0};
	igSeparator();
	bool pressed = igButton("Done", button_dims);
	igEnd();

	if (!pressed) {
		return;
	}

	cs->base.render = scene_render_video;

	struct xrt_frame_sink *rgb = NULL;
	struct xrt_frame_sink *raw = NULL;
	struct xrt_frame_sink *cali = NULL;

	p->texs[p->num_texs++] = gui_ogl_sink_create("Calibration", cs->xfctx, &rgb);
	u_sink_create_to_r8g8b8_or_l8(cs->xfctx, rgb, &rgb);
	u_sink_queue_create(cs->xfctx, rgb, &rgb);

	p->texs[p->num_texs++] = gui_ogl_sink_create("Raw", cs->xfctx, &raw);
	u_sink_create_to_r8g8b8_or_l8(cs->xfctx, raw, &raw);
	u_sink_queue_create(cs->xfctx, raw, &raw);

	t_calibration_stereo_create(cs->xfctx, &cs->params, &cs->status, rgb, &cali);
	u_sink_split_create(cs->xfctx, raw, cali, &cali);
	u_sink_deinterleaver_create(cs->xfctx, cali, &cali);
	u_sink_queue_create(cs->xfctx, cali, &cali);

	// Just after the camera create a quirk stream.
	struct u_sink_quirk_params qp;
	U_ZERO(&qp);
	qp.stereo_sbs = cs->params.stereo_sbs;
	qp.ps4_cam = cs->settings->camera_type == XRT_SETTINGS_CAMERA_TYPE_PS4;
	qp.leap_motion = cs->settings->camera_type == XRT_SETTINGS_CAMERA_TYPE_LEAP_MOTION;
	u_sink_quirk_create(cs->xfctx, cali, &qp, &cali);

	// Now that we have setup a node graph, start it.
	xrt_fs_stream_start(cs->xfs, cali, XRT_FS_CAPTURE_TYPE_CALIBRATION, cs->settings->camera_mode);
#else
	gui_scene_delete_me(p, &cs->base);
#endif
}

static void
scene_destroy(struct gui_scene *scene, struct gui_program *p)
{
	struct calibration_scene *cs = (struct calibration_scene *)scene;

	if (cs->xfctx != NULL) {
		xrt_frame_context_destroy_nodes(cs->xfctx);
		cs->xfctx = NULL;
	}

	if (cs->settings != NULL) {
		free(cs->settings);
		cs->settings = NULL;
	}
#ifdef XRT_HAVE_OPENCV
	// Free data, no longer needed.
	t_stereo_camera_calibration_reference(&cs->status.stereo_data, NULL);
#endif
	free(cs);
}


/*
 *
 * 'Exported' functions.
 *
 */

void
gui_scene_calibrate(struct gui_program *p,
                    struct xrt_frame_context *xfctx,
                    struct xrt_fs *xfs,
                    struct xrt_settings_tracking *s)
{
	struct calibration_scene *cs = U_TYPED_CALLOC(struct calibration_scene);

	cs->base.render = scene_render_select;
	cs->base.destroy = scene_destroy;
	cs->xfctx = xfctx;
	cs->xfs = xfs;
	cs->settings = s;

#ifdef XRT_HAVE_OPENCV
	t_calibration_params_default(&cs->params);

	/*
	 * Pre-quirk some known cameras.
	 */

	// PS4 Camera.
	if (strcmp(xfs->name, "USB Camera-OV580: USB Camera-OV") == 0) {
		// It's one speedy camera. :)
		cs->params.num_cooldown_frames = 240;
		cs->params.num_wait_for = 10;
		cs->params.stereo_sbs = true;
		cs->settings->camera_type = XRT_SETTINGS_CAMERA_TYPE_PS4;
		snprintf(cs->filename, sizeof(cs->filename), "PS4");
	}

	// Leap Motion.
	if (strcmp(xfs->name, "Leap Motion Controller") == 0) {
		cs->params.use_fisheye = true;
		cs->params.stereo_sbs = true;
		cs->settings->camera_type = XRT_SETTINGS_CAMERA_TYPE_LEAP_MOTION;
		snprintf(cs->filename, sizeof(cs->filename), "LeapMotion");
	}

	bool valve = strcmp(xfs->name, "3D Camera: eTronVideo") == 0;
	bool elp = strcmp(xfs->name, "3D USB Camera: 3D USB Camera") == 0;

	if (valve) {
		snprintf(cs->filename, sizeof(cs->filename), "Index");
	}
	if (elp) {
		snprintf(cs->filename, sizeof(cs->filename), "ELP");
	}

	// Valve Index and ELP Stereo Camera.
	if (valve || elp) {
		cs->params.use_fisheye = true;
		cs->params.stereo_sbs = true;
		cs->settings->camera_type = XRT_SETTINGS_CAMERA_TYPE_REGULAR_SBS;
	}
#endif
	gui_scene_push_front(p, &cs->base);
}
