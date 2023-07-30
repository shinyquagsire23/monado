// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Play EuRoC datasets and track them with the SLAM tracker.
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup drv_euroc
 */

#include "euroc_driver.h"
#include "os/os_threading.h"
#include "os/os_time.h"
#include "tracking/t_tracking.h"
#include "util/u_logging.h"
#include "util/u_misc.h"

#include "xrt/xrt_config_have.h"
#include "xrt/xrt_config_build.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_frame.h"
#include "xrt/xrt_frameserver.h"
#include "xrt/xrt_tracking.h"

#if !defined(XRT_FEATURE_SLAM)

void
euroc_run_dataset(const char *euroc_path,
                  const char *slam_config,
                  const char *output_path,
                  const volatile bool *should_exit)
{}

#else

static struct euroc_player_config *
make_euroc_player_config(const char *euroc_path)
{
	struct euroc_player_config *ep_config = U_TYPED_CALLOC(struct euroc_player_config);
	euroc_player_fill_default_config_for(ep_config, euroc_path);

	// Override config to be friendlier for CLI runs unless they were explicitly provided
	if (getenv("EUROC_LOG") == NULL) {
		ep_config->log_level = U_LOGGING_INFO;
	}
	if (getenv("EUROC_PLAY_FROM_START") == NULL) {
		ep_config->playback.play_from_start = true;
	}
	if (getenv("EUROC_PRINT_PROGRESS") == NULL) {
		ep_config->playback.print_progress = true;
	}
	if (getenv("EUROC_USE_SOURCE_TS") == NULL) {
		ep_config->playback.use_source_ts = true;
	}
	if (getenv("EUROC_MAX_SPEED") == NULL) {
		ep_config->playback.max_speed = true;
	}

	return ep_config;
}

static struct t_slam_tracker_config *
make_slam_tracker_config(const char *slam_config, const char *output_path)
{
	struct t_slam_tracker_config *st_config = U_TYPED_CALLOC(struct t_slam_tracker_config);
	t_slam_fill_default_config(st_config);

	// Override config to be friendlier for CLI runs unless they were explicitly provided
	if (getenv("SLAM_LOG") == NULL) {
		st_config->log_level = U_LOGGING_INFO;
	}
	if (getenv("SLAM_SUBMIT_FROM_START") == NULL) {
		st_config->submit_from_start = true;
	}
	if (getenv("SLAM_PREDICTION_TYPE") == NULL) {
		st_config->prediction = SLAM_PRED_NONE;
	}
	if (getenv("SLAM_WRITE_CSVS") == NULL) {
		st_config->write_csvs = true;
	}

	st_config->slam_config = slam_config;
	st_config->csv_path = output_path;

	return st_config;
}

void
euroc_run_dataset(const char *euroc_path,
                  const char *slam_config,
                  const char *output_path,
                  const volatile bool *should_exit)
{
	struct euroc_player_config *ep_config = make_euroc_player_config(euroc_path);
	struct t_slam_tracker_config *st_config = make_slam_tracker_config(slam_config, output_path);
	st_config->cam_count = ep_config->dataset.cam_count;

	// Frame context that will manage SLAM tracker and euroc player lifetimes
	struct xrt_frame_context xfctx = {0};

	// Start SLAM tracker
	struct xrt_tracked_slam *xts = NULL;
	struct xrt_slam_sinks *sinks = NULL;
	int ret = t_slam_create(&xfctx, st_config, &xts, &sinks);
	EUROC_ASSERT(ret == 0, "Failed to create slam tracker");
	t_slam_start(xts);

	// Stream euroc player into the tracker
	struct xrt_fs *xfs = euroc_player_create(&xfctx, euroc_path, ep_config);
	xrt_fs_slam_stream_start(xfs, sinks);

	// Let's loop until both the player and the tracker finish

	// Last two tracked poses, if they are the same we assume tracking stopped
	struct xrt_space_relation a = {0};
	struct xrt_space_relation b = {0};
	b.pose.orientation.w = 42; // Make b different from a

	bool tracking = true;
	bool streaming = xrt_fs_is_running(xfs);
	while ((streaming || tracking) && !*should_exit) {
		os_nanosleep(0.2 * U_TIME_1S_IN_NS);
		a = b;
		xrt_tracked_slam_get_tracked_pose(xts, os_monotonic_get_ns(), &b);
		tracking = memcmp(&a, &b, sizeof(struct xrt_space_relation)) != 0;
		streaming = xrt_fs_is_running(xfs);
	}

	xrt_frame_context_destroy_nodes(&xfctx);
	free(st_config);
	free(ep_config);
}

#endif
