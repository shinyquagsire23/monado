// Copyright 2020-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vive calibration getters.
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_vive
 */

#include "math/m_api.h"
#include "util/u_debug.h"
#include "tracking/t_tracking.h"

#include "vive_config.h"


/*
 *
 * Defines and debug options.
 *
 */

#define VIVE_TRACE(d, ...) U_LOG_IFL_T(d->log_level, __VA_ARGS__)
#define VIVE_DEBUG(d, ...) U_LOG_IFL_D(d->log_level, __VA_ARGS__)
#define VIVE_INFO(d, ...) U_LOG_IFL_I(d->log_level, __VA_ARGS__)
#define VIVE_WARN(d, ...) U_LOG_IFL_W(d->log_level, __VA_ARGS__)
#define VIVE_ERROR(d, ...) U_LOG_IFL_E(d->log_level, __VA_ARGS__)

DEBUG_GET_ONCE_BOOL_OPTION(vive_use_factory_rotations, "VIVE_USE_FACTORY_ROTATIONS", false)


/*
 *
 * Helpers
 *
 */

static struct t_camera_calibration
vive_get_camera_calibration(const struct vive_config *d, int cam_index)
{
	struct t_camera_calibration calib;

	const struct index_camera *camera = &d->cameras.view[cam_index];
	calib.image_size_pixels.w = camera->intrinsics.image_size_pixels.w;
	calib.image_size_pixels.h = camera->intrinsics.image_size_pixels.h;

	calib.intrinsics[0][0] = camera->intrinsics.focal_x;
	calib.intrinsics[0][1] = 0.0f;
	calib.intrinsics[0][2] = camera->intrinsics.center_x;

	calib.intrinsics[1][0] = 0.0f;
	calib.intrinsics[1][1] = camera->intrinsics.focal_y;
	calib.intrinsics[1][2] = camera->intrinsics.center_y;

	calib.intrinsics[2][0] = 0.0f;
	calib.intrinsics[2][1] = 0.0f;
	calib.intrinsics[2][2] = 1.0f;

	calib.distortion_model = T_DISTORTION_FISHEYE_KB4;
	calib.kb4.k1 = camera->intrinsics.distortion[0];
	calib.kb4.k2 = camera->intrinsics.distortion[1];
	calib.kb4.k3 = camera->intrinsics.distortion[2];
	calib.kb4.k4 = camera->intrinsics.distortion[3];

	return calib;
}


/*
 *
 * 'Exported' functions.
 *
 */

bool
vive_get_stereo_camera_calibration(const struct vive_config *d,
                                   struct t_stereo_camera_calibration **calibration_ptr_to_ref,
                                   struct xrt_pose *out_head_in_left_camera)
{
	if (!d->cameras.valid) {
		VIVE_ERROR(d, "Camera config not loaded, cannot produce camera calibration.");
		return false;
	}

	struct t_stereo_camera_calibration *calib = NULL;

	t_stereo_camera_calibration_alloc(&calib, T_DISTORTION_FISHEYE_KB4);

	for (int i = 0; i < 2; i++) {
		calib->view[i] = vive_get_camera_calibration(d, i);
	}

	struct xrt_vec3 pos = d->cameras.opencv.position;
	struct xrt_vec3 x = XRT_VEC3_UNIT_X;
	struct xrt_vec3 y = XRT_VEC3_UNIT_Y;
	struct xrt_vec3 z = XRT_VEC3_UNIT_Z;
	math_quat_rotate_vec3(&d->cameras.opencv.orientation, &x, &x);
	math_quat_rotate_vec3(&d->cameras.opencv.orientation, &y, &y);
	math_quat_rotate_vec3(&d->cameras.opencv.orientation, &z, &z);

	calib->camera_translation[0] = pos.x;
	calib->camera_translation[1] = pos.y;
	calib->camera_translation[2] = pos.z;

	calib->camera_rotation[0][0] = x.x;
	calib->camera_rotation[0][1] = x.y;
	calib->camera_rotation[0][2] = x.z;

	calib->camera_rotation[1][0] = y.x;
	calib->camera_rotation[1][1] = y.y;
	calib->camera_rotation[1][2] = y.z;

	calib->camera_rotation[2][0] = z.x;
	calib->camera_rotation[2][1] = z.y;
	calib->camera_rotation[2][2] = z.z;

	math_pose_invert(&d->cameras.view[0].headref, out_head_in_left_camera);

	// Correctly reference count.
	t_stereo_camera_calibration_reference(calibration_ptr_to_ref, calib);
	t_stereo_camera_calibration_reference(&calib, NULL);

	return true;
}

//! Camera calibrations for SLAM
void
vive_get_slam_cams_calib(const struct vive_config *d,
                         struct t_slam_camera_calibration *out_calib0,
                         struct t_slam_camera_calibration *out_calib1)
{
	VIVE_WARN(d, "Using default factory extrinsics data for vive driver.");
	VIVE_WARN(d, "The rotations of the sensors in the factory data are off.");
	VIVE_WARN(d, "Use a custom calibration instead whenever possible.");

	struct xrt_pose P_tr_imu = d->imu.trackref;
	struct xrt_pose P_tr_cam0 = d->cameras.view[0].trackref;
	struct xrt_pose P_tr_cam1 = d->cameras.view[1].trackref;
	struct xrt_pose P_imu_tr = {0};
	struct xrt_quat Q_imu_tr = {0};
	struct xrt_quat Q_tr_oxr = {.x = 0, .y = 1, .z = 0, .w = 0}; // TR is X: Left, Y: Up, Z: Forward
	struct xrt_quat Q_imu_oxr = {0};
	struct xrt_pose P_imu_imuxr = {0};
	struct xrt_pose P_imuxr_imu = {0};
	struct xrt_quat Q_tr_camslam = {.x = 0, .y = 0, .z = 1, .w = 0}; // Many follow X: Right, Y: Down, Z: Forward
	struct xrt_pose P_cam0_tr = {0};
	struct xrt_quat Q_cam0_tr = {0};
	struct xrt_quat Q_cam0_camslam = {0};
	struct xrt_pose P_cam0_cam0slam = {0};
	struct xrt_pose P_cam1_tr = {0};
	struct xrt_quat Q_cam1_tr = {0};
	struct xrt_quat Q_cam1_camslam = {0};
	struct xrt_pose P_cam1_cam1slam = {0};
	struct xrt_pose P_imu_cam0 = {0};
	struct xrt_pose P_imuxr_cam0 = {0};
	struct xrt_pose P_imuxr_cam0slam = {0};
	struct xrt_pose P_imu_cam1 = {0};
	struct xrt_pose P_imuxr_cam1 = {0};
	struct xrt_pose P_imuxr_cam1slam = {0};
	struct xrt_matrix_4x4 T_imu_cam0 = {0};
	struct xrt_matrix_4x4 T_imu_cam1 = {0};

	// Compute P_imuxr_imu. imuxr is same entity as IMU but with axes like OpenXR
	// E.g., for Index IMU has X: down, Y: left, Z: forward
	math_pose_invert(&P_tr_imu, &P_imu_tr);
	Q_imu_tr = P_imu_tr.orientation;
	math_quat_rotate(&Q_imu_tr, &Q_tr_oxr, &Q_imu_oxr);
	P_imu_imuxr.orientation = Q_imu_oxr;
	math_pose_invert(&P_imu_imuxr, &P_imuxr_imu);

	// Compute P_imuxr_cam0slam. cam0slam is the same entity as cam0 but with axes
	// like the SLAM system expects cameras to be. Usually X: Right, Y: Down, Z: Forward.
	math_pose_invert(&P_tr_cam0, &P_cam0_tr);
	Q_cam0_tr = P_cam0_tr.orientation;
	math_quat_rotate(&Q_cam0_tr, &Q_tr_camslam, &Q_cam0_camslam);
	P_cam0_cam0slam.orientation = Q_cam0_camslam;
	math_pose_transform(&P_imu_tr, &P_tr_cam0, &P_imu_cam0);
	math_pose_transform(&P_imuxr_imu, &P_imu_cam0, &P_imuxr_cam0);
	math_pose_transform(&P_imuxr_cam0, &P_cam0_cam0slam, &P_imuxr_cam0slam);

	// Compute P_imuxr_cam1slam. Same idea as P_imuxr_cam0slam
	math_pose_invert(&P_tr_cam1, &P_cam1_tr);
	Q_cam1_tr = P_cam1_tr.orientation;
	math_quat_rotate(&Q_cam1_tr, &Q_tr_camslam, &Q_cam1_camslam);
	P_cam1_cam1slam.orientation = Q_cam1_camslam;
	math_pose_transform(&P_imu_tr, &P_tr_cam1, &P_imu_cam1);
	math_pose_transform(&P_imuxr_imu, &P_imu_cam1, &P_imuxr_cam1);
	math_pose_transform(&P_imuxr_cam1, &P_cam1_cam1slam, &P_imuxr_cam1slam);

	bool use_factory_rots = debug_get_bool_option_vive_use_factory_rotations();
	if (!use_factory_rots) {
		//! @todo: Index factory calibration is weird and doesn't seem to have the
		//! proper extrinsics. Let's overwrite them with some extrinsics
		//! I got from doing a calibration on my own headset. These seem to work
		//! better than native values. (@mateosss)
		P_imuxr_cam0slam.orientation.x = 0.999206844251353;
		P_imuxr_cam0slam.orientation.y = -0.008523559718599975;
		P_imuxr_cam0slam.orientation.z = -0.038897421992888748;
		P_imuxr_cam0slam.orientation.w = 0.00014796379001732346;

		P_imuxr_cam1slam.orientation.x = 0.9990931516177515;
		P_imuxr_cam1slam.orientation.y = -0.011906493530393766;
		P_imuxr_cam1slam.orientation.z = 0.03990451825243117;
		P_imuxr_cam1slam.orientation.w = 0.008873512571741;
	}

	// Convert to 4x4 SE(3) matrices
	math_matrix_4x4_isometry_from_pose(&P_imuxr_cam0slam, &T_imu_cam0);
	math_matrix_4x4_isometry_from_pose(&P_imuxr_cam1slam, &T_imu_cam1);

	// Can we avoid hardcoding camera frequency?
	const int CAMERA_FREQUENCY = 54;

	struct t_slam_camera_calibration calib0 = {
	    .base = vive_get_camera_calibration(d, 0),
	    .frequency = CAMERA_FREQUENCY,
	    .T_imu_cam = T_imu_cam0,
	};

	struct t_slam_camera_calibration calib1 = {
	    .base = vive_get_camera_calibration(d, 1),
	    .frequency = CAMERA_FREQUENCY,
	    .T_imu_cam = T_imu_cam1,
	};

	*out_calib0 = calib0;
	*out_calib1 = calib1;
}

void
vive_get_imu_calibration(const struct vive_config *d, struct t_imu_calibration *out_calib)
{

	struct xrt_vec3 ab = d->imu.acc_bias;
	struct xrt_vec3 as = d->imu.acc_scale;
	struct xrt_vec3 gb = d->imu.gyro_bias;
	struct xrt_vec3 gs = d->imu.gyro_scale;

	struct t_imu_calibration calib = {
	    .accel =
	        {
	            .transform = {{as.x, 0, 0}, {0, as.y, 0}, {0, 0, as.z}},
	            .offset = {-ab.x, -ab.y, -ab.z}, // negative because slam system will add, not subtract
	            .bias_std = {0.001, 0.001, 0.001},
	            .noise_std = {0.016, 0.016, 0.016},
	        },
	    .gyro =
	        {
	            .transform = {{gs.x, 0, 0}, {0, gs.y, 0}, {0, 0, gs.z}},
	            .offset = {-gb.x, -gb.y, -gb.z}, // negative because slam system will add, not subtract
	            .bias_std = {0.0001, 0.0001, 0.0001},
	            .noise_std = {0.000282, 0.000282, 0.000282},
	        },
	};

	*out_calib = calib;
}

void
vive_get_slam_imu_calibration(const struct vive_config *d, struct t_slam_imu_calibration *out_calib)
{
	struct t_slam_imu_calibration calib;
	const int IMU_FREQUENCY = 1000;

	vive_get_imu_calibration(d, &calib.base);
	calib.frequency = IMU_FREQUENCY;

	*out_calib = calib;
}
