// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface for @ref drv_euroc
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup drv_euroc
 */

#pragma once

#include "util/u_logging.h"
#include "xrt/xrt_frameserver.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @defgroup drv_euroc Euroc driver
 * @ingroup drv
 *
 * @brief Provide euroc dataset playback features for SLAM evaluation
 *
 * This driver works with any dataset using the EuRoC format.
 * Original EuRoC datasets and more information about them can be found in
 * https://projects.asl.ethz.ch/datasets/doku.php?id=kmavvisualinertialdatasets
 */

/*!
 * Playback configuration for the euroc player.
 *
 * @ingroup drv_euroc
 */
struct euroc_player_playback_config
{
	int cam_count;            //!< Number of cameras to stream from the dataset
	bool color;               //!< If RGB available but this is false, images will be loaded in grayscale
	bool gt;                  //!< Whether to send groundtruth data (if available) to the SLAM tracker
	bool skip_perc;           //!< Whether @ref skip_first represents percentage or seconds
	float skip_first;         //!< How much of the first dataset samples to skip, @see skip_perc
	float scale;              //!< Scale of each frame; e.g., 0.5 (half), 1.0 (avoids resize)
	bool max_speed;           //!< If true, push samples as fast as possible, other wise @see speed
	double speed;             //!< Intended reproduction speed if @ref max_speed is false
	bool send_all_imus_first; //!< If enabled all imu samples will be sent before img samples
	bool paused;              //!< Whether to pause the playback
	bool use_source_ts;       //!< If true, use the original timestamps from the dataset
	bool play_from_start;     //!< If set, the euroc player does not wait for user input to start
	bool print_progress;      //!< Whether to print progress to stdout (useful for CLI runs)
};

/*!
 * Describes information about a particular EuRoC dataset residing in `path`.
 *
 * @ingroup drv_euroc
 */
struct euroc_player_dataset_info
{
	char path[256];
	int cam_count;
	bool is_colored;
	bool has_gt; //!< Whether this dataset has groundtruth data available
	const char *gt_device_name;
	uint32_t width;
	uint32_t height;
};

/*!
 * Configuration for the euroc player.
 *
 * @ingroup drv_euroc
 */
struct euroc_player_config
{
	enum u_logging_level log_level;
	struct euroc_player_dataset_info dataset;
	struct euroc_player_playback_config playback;
};

/*!
 * Fills in an @ref euroc_player_config with defaults based on the provided dataset path.
 *
 * @ingroup drv_euroc
 */
void
euroc_player_fill_default_config_for(struct euroc_player_config *config, const char *path);

/*!
 * Create an euroc player from a path to a dataset.
 *
 * @ingroup drv_euroc
 */
struct xrt_fs *
euroc_player_create(struct xrt_frame_context *xfctx, const char *path, struct euroc_player_config *config);

/*!
 * Create a auto prober for the fake euroc device.
 *
 * @ingroup drv_euroc
 */
struct xrt_auto_prober *
euroc_create_auto_prober(void);

/*!
 * Tracks an euroc dataset with the SLAM tracker.
 *
 * @param should_exit External exit condition, the run will end if it becomes true
 * @param euroc_path Dataset path
 * @param slam_config Path to config file for the SLAM system
 * @param output_path Path to write resulting tracking data to
 *
 * @ingroup drv_euroc
 */
void
euroc_run_dataset(const char *euroc_path,
                  const char *slam_config,
                  const char *output_path,
                  const volatile bool *should_exit);

/*!
 * @dir drivers/euroc
 *
 * @brief @ref drv_euroc files.
 */

#ifdef __cplusplus
}
#endif
