// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  PSVR tracker code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Pete Black <pete.black@collabora.com>
 * @ingroup aux_tracking
 */

#include "xrt/xrt_tracking.h"

#include "tracking/t_tracking.h"
#include "tracking/t_calibration_opencv.hpp"
#include "tracking/t_helper_debug_sink.hpp"

#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_frame.h"
#include "util/u_format.h"
#include "util/u_var.h"
#include "util/u_logging.h"

#include "math/m_api.h"
#include "math/m_permutation.h"

#include "os/os_threading.h"

#include <stdio.h>
#include <assert.h>
#include <pthread.h>

#include <Hungarian.hpp>
#include <Eigen/Eigen>
#include <opencv2/opencv.hpp>
#include <inttypes.h>


DEBUG_GET_ONCE_LOG_OPTION(psvr_log, "PSVR_TRACKING_LOG", U_LOGGING_WARN)

#define PSVR_TRACE(...) U_LOG_IFL_T(t.ll, __VA_ARGS__)
#define PSVR_DEBUG(...) U_LOG_IFL_D(t.ll, __VA_ARGS__)
#define PSVR_INFO(...) U_LOG_IFL_I(t.ll, __VA_ARGS__)
#define PSVR_WARN(...) U_LOG_IFL_W(t.ll, __VA_ARGS__)
#define PSVR_ERROR(...) U_LOG_IFL_E(t.ll, __VA_ARGS__)


/*!
 * How many LEDs in the tracked configuration
 */
#define PSVR_NUM_LEDS 7
/*!
 * How many LEDs do we need to do an optical solve/correction
 */
#define PSVR_OPTICAL_SOLVE_THRESH 5
/*!
 * If potential match vertex is further than this distance from the
 * measurement, reject the match - do not set too low
 */
#define PSVR_DISAMBIG_REJECT_DIST 0.02f
/*!
 * If potential match vertex is further than this distance from the measurement,
 * reject the match - do not set too low
 */
#define PSVR_DISAMBIG_REJECT_ANG 0.7f
/*!
 * Cutoff distance for keeping the id for a blob from one frame to the next
 */
#define PSVR_SEARCH_RADIUS 0.043f
/*
 * The magnitude of the correction relative to the previous correction must be
 * below this value to contribute towards lock acquisition
 */
#define PSVR_MAX_BAD_CORR 10
#define PSVR_BAD_CORRECTION_THRESH 0.1f
#define PSVR_CORRECTION_THRESH 0.05f

/*!
 * We will 'drift' our imu-solved rotation towards our optically solved
 * correction to avoid jumps
 */
#define PSVR_FAST_CORRECTION 0.05f

/*!
 * We will 'drift' our imu-solved rotation towards our optically solved
 * correction to avoid jumps
 */
#define PSVR_SLOW_CORRECTION 0.005f

// kalman filter coefficients
#define PSVR_BLOB_PROCESS_NOISE 0.1f     // R
#define PSVR_BLOB_MEASUREMENT_NOISE 1.0f // Q

#define PSVR_POSE_PROCESS_NOISE 0.5f // R
//! Our measurements are quite noisy so we need to smooth heavily
#define PSVR_POSE_MEASUREMENT_NOISE 100.0f

#define PSVR_OUTLIER_THRESH 0.17f
#define PSVR_MERGE_THRESH 0.06f

//! hold the previously recognised configuration unless we depart significantly
#define PSVR_HOLD_THRESH 0.086f

// uncomment this to dump comprehensive optical and imu data to
// /tmp/psvr_dump.txt

//#define PSVR_DUMP_FOR_OFFLINE_ANALYSIS
//#define PSVR_DUMP_IMU_FOR_OFFLINE_ANALYSIS

typedef enum blob_type
{
	BLOB_TYPE_UNKNOWN,
	BLOB_TYPE_SIDE,
	BLOB_TYPE_FRONT,
	BLOB_TYPE_REAR, // currently unused
} blob_type_t;

typedef struct blob_point
{
	cv::Point3f p;     // 3d coordinate
	cv::KeyPoint lkp;  // left keypoint
	cv::KeyPoint rkp;  // right keypoint
	blob_type_t btype; // blob type
} blob_point_t;

struct View
{
	cv::Mat undistort_rectify_map_x;
	cv::Mat undistort_rectify_map_y;

	cv::Matx33d intrinsics;
	cv::Mat distortion; // size may vary
	cv::Vec4d distortion_fisheye;
	bool use_fisheye;

	std::vector<cv::KeyPoint> keypoints;

	cv::Mat frame_undist_rectified;

	void
	populate_from_calib(t_camera_calibration &calib, const RemapPair &rectification)
	{
		CameraCalibrationWrapper wrap(calib);
		intrinsics = wrap.intrinsics_mat;
		distortion = wrap.distortion_mat.clone();
		distortion_fisheye = wrap.distortion_fisheye_mat;
		use_fisheye = wrap.use_fisheye;

		undistort_rectify_map_x = rectification.remap_x;
		undistort_rectify_map_y = rectification.remap_y;
	}
};

typedef enum led_tag
{
	TAG_TL,
	TAG_TR,
	TAG_C,
	TAG_BL,
	TAG_BR,
	TAG_SL,
	TAG_SR
} led_tag_t;

typedef struct model_vertex
{
	int32_t vertex_index;
	Eigen::Vector4f position;

	led_tag_t tag;
	bool active;

	// NOTE: these operator overloads are required for
	// comparisons with the permutations library

	bool
	operator<(const model_vertex &mv) const
	{
		return (vertex_index < mv.vertex_index);
	}
	bool
	operator>(const model_vertex &mv) const
	{
		return (vertex_index > mv.vertex_index);
	}

} model_vertex_t;

typedef struct match_data
{
	float angle = {};              // angle from reference vector
	float distance = {};           // distance from base of reference vector
	int32_t vertex_index = {};     // index aka tag
	Eigen::Vector4f position = {}; // 3d position of vertex
	blob_point_t src_blob = {};    // blob this vertex was derived from
} match_data_t;

typedef struct match_model
{
	//! Collection of vertices and associated data.
	std::vector<match_data_t> measurements;
} match_model_t;

/*!
 * Main PSVR tracking class.
 */
class TrackerPSVR
{
public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
	struct xrt_tracked_psvr base = {};
	struct xrt_frame_sink sink = {};
	struct xrt_frame_node node = {};

	//! Logging stuff.
	enum u_logging_level ll;

	//! Frame waiting to be processed.
	struct xrt_frame *frame;

	//! Thread and lock helper.
	struct os_thread_helper oth;

	//! Have we received a new IMU sample.
	bool has_imu = false;

	timepoint_ns last_imu{0};

	struct
	{
		struct xrt_vec3 pos = {};
		struct xrt_quat rot = {};
	} fusion;

	struct
	{
		struct xrt_vec3 pos = {};
		struct xrt_quat rot = {};
	} optical;

	Eigen::Quaternionf target_optical_rotation_correction; // the calculated rotation to
	                                                       // correct the imu
	Eigen::Quaternionf optical_rotation_correction;        // currently applied (interpolated
	                                                       // towards target) correction
	Eigen::Matrix4f corrected_imu_rotation;                // imu rotation with correction applied
	Eigen::Quaternionf axis_align_rot;                     // used to rotate imu/tracking coordinates to world

	model_vertex_t model_vertices[PSVR_NUM_LEDS]; // the model we match our
	                                              // measurements against
	std::vector<match_data_t> last_vertices;      // the last solved position of the HMD

	uint32_t last_optical_model;

	cv::KalmanFilter track_filters[PSVR_NUM_LEDS];


	cv::KalmanFilter pose_filter; // we filter the final pose position of
	                              // the HMD to smooth motion

	View view[2];
	bool calibrated;

	HelperDebugSink debug = {HelperDebugSink::AllAvailable};

	cv::Mat disparity_to_depth;
	cv::Vec3d r_cam_translation;
	cv::Matx33d r_cam_rotation;

	cv::Ptr<cv::SimpleBlobDetector> sbd;
	std::vector<cv::KeyPoint> l_blobs, r_blobs;
	std::vector<match_model_t> matches;

	// we refine our measurement by rejecting outliers and merging 'too
	// close' points
	std::vector<blob_point_t> world_points;
	std::vector<blob_point_t> pruned_points;
	std::vector<blob_point_t> merged_points;

	std::vector<match_data_t> match_vertices;

	float avg_optical_correction; // used to converge to a 'lock' correction
	                              // rotation

	bool done_correction; // set after a 'lock' is acquired

	float max_correction;

	// if we have made a lot of optical measurements that *should*
	// be converging, but have not - we should reset
	uint32_t bad_correction_count;

	Eigen::Matrix4f last_pose;

	uint64_t last_frame;

	Eigen::Vector4f model_center; // center of rotation

#ifdef PSVR_DUMP_FOR_OFFLINE_ANALYSIS
	FILE *dump_file;
#endif
};

static float
dist_3d(Eigen::Vector4f a, Eigen::Vector4f b)
{
	return sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

static float
dist_3d_cv(cv::Point3f a, cv::Point3f b)
{
	return sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

static void
init_filter(cv::KalmanFilter &kf, float process_cov, float meas_cov, float dt)
{
	kf.init(6, 3);
	kf.transitionMatrix =
	    (cv::Mat_<float>(6, 6) << 1.0, 0.0, 0.0, dt, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, dt, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
	     dt, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0);

	cv::setIdentity(kf.measurementMatrix, cv::Scalar::all(1.0f));
	cv::setIdentity(kf.errorCovPost, cv::Scalar::all(0.0f));

	// our filter parameters set the process and measurement noise
	// covariances.

	cv::setIdentity(kf.processNoiseCov, cv::Scalar::all(process_cov));
	cv::setIdentity(kf.measurementNoiseCov, cv::Scalar::all(meas_cov));
}

static void
filter_predict(std::vector<match_data_t> *pose, cv::KalmanFilter *filters, float dt)
{
	for (uint32_t i = 0; i < PSVR_NUM_LEDS; i++) {
		match_data_t current_led;
		cv::KalmanFilter *current_kf = filters + i;

		// set our dt components in the transition matrix
		current_kf->transitionMatrix.at<float>(0, 3) = dt;
		current_kf->transitionMatrix.at<float>(1, 4) = dt;
		current_kf->transitionMatrix.at<float>(2, 5) = dt;

		current_led.vertex_index = i;
		// current_led->tag = (led_tag_t)(i);
		cv::Mat prediction = current_kf->predict();
		current_led.position[0] = prediction.at<float>(0, 0);
		current_led.position[1] = prediction.at<float>(1, 0);
		current_led.position[2] = prediction.at<float>(2, 0);
		pose->push_back(current_led);
	}
}

static void
filter_update(std::vector<match_data_t> *pose, cv::KalmanFilter *filters, float dt)
{
	for (uint32_t i = 0; i < PSVR_NUM_LEDS; i++) {
		match_data_t *current_led = &pose->at(i);
		cv::KalmanFilter *current_kf = filters + i;

		// set our dt components in the transition matrix
		current_kf->transitionMatrix.at<float>(0, 3) = dt;
		current_kf->transitionMatrix.at<float>(1, 4) = dt;
		current_kf->transitionMatrix.at<float>(2, 5) = dt;

		current_led->vertex_index = i;

		cv::Mat measurement = cv::Mat(3, 1, CV_32F);
		measurement.at<float>(0, 0) = current_led->position[0];
		measurement.at<float>(1, 0) = current_led->position[1];
		measurement.at<float>(2, 0) = current_led->position[2];
		current_kf->correct(measurement);
	}
}

static void
pose_filter_predict(Eigen::Vector4f *pose, cv::KalmanFilter *filter, float dt)
{
	// set our dt components in the transition matrix
	filter->transitionMatrix.at<float>(0, 3) = dt;
	filter->transitionMatrix.at<float>(1, 4) = dt;
	filter->transitionMatrix.at<float>(2, 5) = dt;

	cv::Mat prediction = filter->predict();
	(*pose)[0] = prediction.at<float>(0, 0);
	(*pose)[1] = prediction.at<float>(1, 0);
	(*pose)[2] = prediction.at<float>(2, 0);
}

static void
pose_filter_update(Eigen::Vector4f *position, cv::KalmanFilter *filter, float dt)
{
	filter->transitionMatrix.at<float>(0, 3) = dt;
	filter->transitionMatrix.at<float>(1, 4) = dt;
	filter->transitionMatrix.at<float>(2, 5) = dt;

	cv::Mat measurement = cv::Mat(3, 1, CV_32F);
	measurement.at<float>(0, 0) = position->x();
	measurement.at<float>(1, 0) = position->y();
	measurement.at<float>(2, 0) = position->z();
	filter->correct(measurement);
}

static bool
match_possible(match_model_t *match)
{
	//@todo - this is currently unimplemented
	// check if this match makes sense - we can remove
	// unobservable combinations without checking them.


	// we cannot see SR,SL at the same time so remove any matches that
	// contain them both in the first 5 slots
	return true;
}

static void
verts_to_measurement(std::vector<blob_point_t> *meas_data, std::vector<match_data_t> *match_vertices)
{
	// create a data structure that holds the inter-point distances
	// and angles we will use to match the pose

	match_vertices->clear();
	if (meas_data->size() < PSVR_OPTICAL_SOLVE_THRESH) {
		for (uint32_t i = 0; i < meas_data->size(); i++) {
			match_data_t md;
			md.vertex_index = -1;
			md.position =
			    Eigen::Vector4f(meas_data->at(i).p.x, meas_data->at(i).p.y, meas_data->at(i).p.z, 1.0f);
			md.src_blob = meas_data->at(i);
			match_vertices->push_back(md);
		}

		return;
	}

	blob_point_t ref_a = meas_data->at(0);
	blob_point_t ref_b = meas_data->at(1);
	cv::Point3f ref_vec = ref_b.p - ref_a.p;
	float ref_len = dist_3d_cv(ref_a.p, ref_b.p);

	for (uint32_t i = 0; i < meas_data->size(); i++) {
		blob_point_t vp = meas_data->at(i);
		cv::Point3f point_vec = vp.p - ref_a.p;
		match_data_t md;
		md.vertex_index = -1;
		md.position = Eigen::Vector4f(vp.p.x, vp.p.y, vp.p.z, 1.0f);
		Eigen::Vector3f ref_vec3(ref_vec.x, ref_vec.y, ref_vec.z);
		Eigen::Vector3f point_vec3(point_vec.x, point_vec.y, point_vec.z);
		Eigen::Vector3f vp_pos3(vp.p.x, vp.p.y, vp.p.z);

		if (i != 0) {
			Eigen::Vector3f plane_norm = ref_vec3.cross(point_vec3).normalized();
			if (plane_norm.z() > 0) {
				md.angle = -1 * acos(point_vec3.normalized().dot(ref_vec3.normalized()));
			} else {
				md.angle = acos(point_vec3.normalized().dot(ref_vec3.normalized()));
			}

			md.distance = dist_3d_cv(vp.p, ref_a.p) / ref_len;
		} else {
			md.angle = 0.0f;
			md.distance = 0.0f;
		}
		// fix up any NaNs
		if (md.angle != md.angle) {
			md.angle = 0.0f;
		}
		if (md.distance != md.distance) {
			md.distance = 0.0f;
		}
		md.src_blob = vp;
		match_vertices->push_back(md);
	}
}

static float
last_diff(TrackerPSVR &t, std::vector<match_data_t> *meas_pose, std::vector<match_data_t> *last_pose)
{
	// compute the aggregate difference (sum of distances between matching
	// indices)between two poses

	float diff = 0.0f;
	for (uint32_t i = 0; i < meas_pose->size(); i++) {
		uint32_t meas_index = meas_pose->at(i).vertex_index;
		for (uint32_t j = 0; j < last_pose->size(); j++) {
			uint32_t last_index = last_pose->at(j).vertex_index;
			if (last_index == meas_index) {
				float d = fabs(
				    dist_3d(meas_pose->at(meas_index).position, last_pose->at(last_index).position));
				diff += d;
			}
		}
	}
	return diff / meas_pose->size();
}


static void
remove_outliers(std::vector<blob_point_t> *orig_points, std::vector<blob_point_t> *pruned_points, float outlier_thresh)
{

	if (orig_points->size() == 0) {
		return;
	}

	std::vector<blob_point_t> temp_points;

	// immediately prune anything that is measured as
	// 'behind' the camera - often reflections or lights in the room etc.

	for (uint32_t i = 0; i < orig_points->size(); i++) {
		cv::Point3f p = orig_points->at(i).p;
		if (p.z < 0) {
			temp_points.push_back(orig_points->at(i));
		}
	}
	if (temp_points.size() == 0) {
		return;
	}

	// compute the 3d median of the points, and reject anything further away
	// than a
	// threshold distance


	std::vector<float> x_values;
	std::vector<float> y_values;
	std::vector<float> z_values;
	for (uint32_t i = 0; i < temp_points.size(); i++) {
		x_values.push_back(temp_points[i].p.x);
		y_values.push_back(temp_points[i].p.y);
		z_values.push_back(temp_points[i].p.z);
	}

	std::nth_element(x_values.begin(), x_values.begin() + x_values.size() / 2, x_values.end());
	float median_x = x_values[x_values.size() / 2];
	std::nth_element(y_values.begin(), y_values.begin() + y_values.size() / 2, y_values.end());
	float median_y = y_values[y_values.size() / 2];
	std::nth_element(z_values.begin(), z_values.begin() + z_values.size() / 2, z_values.end());
	float median_z = z_values[z_values.size() / 2];

	for (uint32_t i = 0; i < temp_points.size(); i++) {
		float error_x = temp_points[i].p.x - median_x;
		float error_y = temp_points[i].p.y - median_y;
		float error_z = temp_points[i].p.z - median_z;

		float rms_error = sqrt((error_x * error_x) + (error_y * error_y) + (error_z * error_z));

		// U_LOG_D("%f %f %f  %f %f %f", temp_points[i].p.x,
		//       temp_points[i].p.y, temp_points[i].p.z, error_x,
		//       error_y, error_z);
		if (rms_error < outlier_thresh) {
			pruned_points->push_back(temp_points[i]);
		}
	}
}

struct close_pair
{
	int index_a;
	int index_b;
	float dist;
};

static void
merge_close_points(std::vector<blob_point_t> *orig_points, std::vector<blob_point_t> *merged_points, float merge_thresh)
{
	// if a pair of points in the supplied lists are closer than the
	// threshold, discard one of them.

	//@todo - merge the 2d blob extents when we merge a pair of points

	std::vector<struct close_pair> pairs;
	for (uint32_t i = 0; i < orig_points->size(); i++) {
		for (uint32_t j = 0; j < orig_points->size(); j++) {
			if (i != j) {
				float d = dist_3d_cv(orig_points->at(i).p, orig_points->at(j).p);
				if (d < merge_thresh) {
					struct close_pair p;
					p.index_a = i;
					p.index_b = j;
					p.dist = d;

					pairs.push_back(p);
				}
			}
		}
	}
	std::vector<int> indices_to_remove;
	for (uint32_t i = 0; i < pairs.size(); i++) {
		if (pairs[i].index_a < pairs[i].index_b) {
			indices_to_remove.push_back(pairs[i].index_a);
		} else {
			indices_to_remove.push_back(pairs[i].index_b);
		}
	}

	for (int i = 0; i < (int)orig_points->size(); i++) {
		bool remove_index = false;
		for (int j = 0; j < (int)indices_to_remove.size(); j++) {
			if (i == indices_to_remove[j]) {
				remove_index = true;
			}
		}
		if (!remove_index) {
			merged_points->push_back(orig_points->at(i));
		}
	}
}

static void
match_triangles(Eigen::Matrix4f *t1_mat,
                Eigen::Matrix4f *t1_to_t2_mat,
                Eigen::Vector4f t1_a,
                Eigen::Vector4f t1_b,
                Eigen::Vector4f t1_c,
                Eigen::Vector4f t2_a,
                Eigen::Vector4f t2_b,
                Eigen::Vector4f t2_c)
{
	// given 3 vertices in 'model space', and a corresponding 3 vertices
	// in 'world space', compute the transformation matrix to map one
	// to the other

	*t1_mat = Eigen::Matrix4f().Identity();
	Eigen::Matrix4f t2_mat = Eigen::Matrix4f().Identity();

	Eigen::Vector3f t1_x_vec = (t1_b - t1_a).head<3>().normalized();
	Eigen::Vector3f t1_z_vec = (t1_c - t1_a).head<3>().cross((t1_b - t1_a).head<3>()).normalized();
	Eigen::Vector3f t1_y_vec = t1_x_vec.cross(t1_z_vec).normalized();

	Eigen::Vector3f t2_x_vec = (t2_b - t2_a).head<3>().normalized();
	Eigen::Vector3f t2_z_vec = (t2_c - t2_a).head<3>().cross((t2_b - t2_a).head<3>()).normalized();
	Eigen::Vector3f t2_y_vec = t2_x_vec.cross(t2_z_vec).normalized();

	t1_mat->col(0) << t1_x_vec[0], t1_x_vec[1], t1_x_vec[2], 0.0f;
	t1_mat->col(1) << t1_y_vec[0], t1_y_vec[1], t1_y_vec[2], 0.0f;
	t1_mat->col(2) << t1_z_vec[0], t1_z_vec[1], t1_z_vec[2], 0.0f;
	t1_mat->col(3) << t1_a[0], t1_a[1], t1_a[2], 1.0f;

	t2_mat.col(0) << t2_x_vec[0], t2_x_vec[1], t2_x_vec[2], 0.0f;
	t2_mat.col(1) << t2_y_vec[0], t2_y_vec[1], t2_y_vec[2], 0.0f;
	t2_mat.col(2) << t2_z_vec[0], t2_z_vec[1], t2_z_vec[2], 0.0f;
	t2_mat.col(3) << t2_a[0], t2_a[1], t2_a[2], 1.0f;

	*t1_to_t2_mat = t1_mat->inverse() * t2_mat;
}

static Eigen::Matrix4f
solve_for_measurement(TrackerPSVR *t, std::vector<match_data_t> *measurement, std::vector<match_data_t> *solved)
{
	// use the vertex positions (at least 3) in the measurement to
	// construct a pair of triangles which are used to calculate the
	// pose of the tracked HMD,
	// based on the corresponding model vertices

	// @todo: compute all possible unique triangles, and average the result

	Eigen::Matrix4f tri_basis;
	Eigen::Matrix4f model_to_measurement;

	Eigen::Vector4f meas_ref_a = measurement->at(0).position;
	Eigen::Vector4f meas_ref_b = measurement->at(1).position;
	int meas_index_a = measurement->at(0).vertex_index;
	int meas_index_b = measurement->at(1).vertex_index;

	Eigen::Vector4f model_ref_a = t->model_vertices[meas_index_a].position;
	Eigen::Vector4f model_ref_b = t->model_vertices[meas_index_b].position;

	float highest_length = 0.0f;
	int best_model_index = 0;
	int most_distant_index = 0;

	for (uint32_t i = 0; i < measurement->size(); i++) {
		int model_tag_index = measurement->at(i).vertex_index;
		Eigen::Vector4f model_vert = t->model_vertices[model_tag_index].position;
		if (most_distant_index > 1 && dist_3d(model_vert, model_ref_a) > highest_length) {
			best_model_index = most_distant_index;
		}
		most_distant_index++;
	}

	Eigen::Vector4f meas_ref_c = measurement->at(best_model_index).position;
	int meas_index_c = measurement->at(best_model_index).vertex_index;

	Eigen::Vector4f model_ref_c = t->model_vertices[meas_index_c].position;

	match_triangles(&tri_basis, &model_to_measurement, model_ref_a, model_ref_b, model_ref_c, meas_ref_a,
	                meas_ref_b, meas_ref_c);
	Eigen::Matrix4f model_center_transform_f = tri_basis * model_to_measurement * tri_basis.inverse();

	// now reverse the order of our verts to contribute to a more accurate
	// estimate.

	meas_ref_a = measurement->at(measurement->size() - 1).position;
	meas_ref_b = measurement->at(measurement->size() - 2).position;
	meas_index_a = measurement->at(measurement->size() - 1).vertex_index;
	meas_index_b = measurement->at(measurement->size() - 2).vertex_index;

	model_ref_a = t->model_vertices[meas_index_a].position;
	model_ref_b = t->model_vertices[meas_index_b].position;

	highest_length = 0.0f;
	best_model_index = 0;
	most_distant_index = 0;

	for (uint32_t i = 0; i < measurement->size(); i++) {
		int model_tag_index = measurement->at(i).vertex_index;
		Eigen::Vector4f model_vert = t->model_vertices[model_tag_index].position;
		if (most_distant_index < (int)measurement->size() - 2 &&
		    dist_3d(model_vert, model_ref_a) > highest_length) {
			best_model_index = most_distant_index;
		}
		most_distant_index++;
	}

	meas_ref_c = measurement->at(best_model_index).position;
	meas_index_c = measurement->at(best_model_index).vertex_index;

	model_ref_c = t->model_vertices[meas_index_c].position;

	match_triangles(&tri_basis, &model_to_measurement, model_ref_a, model_ref_b, model_ref_c, meas_ref_a,
	                meas_ref_b, meas_ref_c);
	Eigen::Matrix4f model_center_transform_r = tri_basis * model_to_measurement * tri_basis.inverse();

	// decompose our transforms and slerp between them to get the avg of the
	// rotation determined from the first 2 + most distant , and last 2 +
	// most distant verts

	Eigen::Matrix3f r = model_center_transform_f.block(0, 0, 3, 3);
	Eigen::Quaternionf f_rot_part = Eigen::Quaternionf(r);
	Eigen::Vector4f f_trans_part = model_center_transform_f.col(3);

	r = model_center_transform_r.block(0, 0, 3, 3);
	Eigen::Quaternionf r_rot_part = Eigen::Quaternionf(r);
	Eigen::Vector4f r_trans_part = model_center_transform_r.col(3);

	Eigen::Matrix4f pose = Eigen::Matrix4f().Identity();
	pose.block(0, 0, 3, 3) = f_rot_part.slerp(0.5, r_rot_part).toRotationMatrix();
	pose.col(3) = (f_trans_part + r_trans_part) / 2.0f;

	solved->clear();
	for (uint32_t i = 0; i < PSVR_NUM_LEDS; i++) {
		match_data_t md;
		md.vertex_index = i;
		md.position = pose * t->model_vertices[i].position;
		solved->push_back(md);
	}

	return pose;
}

typedef struct proximity_data
{
	Eigen::Vector4f position;
	float lowest_distance;
	int vertex_index;
} proximity_data_t;

static Eigen::Matrix4f
solve_with_imu(TrackerPSVR &t,
               std::vector<match_data_t> *measurements,
               std::vector<match_data_t> *match_measurements,
               std::vector<match_data_t> *solved,
               float search_radius)
{

	// use the hungarian algorithm to find the closest set of points to the
	// match measurement

	// a 7x7 matrix of costs e.g distances between our points and the match
	// measurements we will initialise to zero because we will not have
	// distances for points we don't have

	std::vector<vector<double> > costMatrix = {
	    {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0},
	    {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0},
	};

	HungarianAlgorithm HungAlgo;
	vector<int> assignment;

	// lets fill in our cost matrix with distances
	// @todo: could use squared distance to save a handful of sqrts.

	// @todo: artificially boost cost where distance from last exceeds
	// search threshold
	// @todo: artificially boost cost where blob type differs from match
	// measurement

	for (uint32_t i = 0; i < measurements->size(); i++) {
		for (uint32_t j = 0; j < match_measurements->size(); j++) {
			costMatrix[i][j] = dist_3d(measurements->at(i).position, match_measurements->at(j).position);
			if (measurements->at(i).src_blob.btype == BLOB_TYPE_SIDE &&
			    match_measurements->at(j).src_blob.btype == BLOB_TYPE_FRONT) {
				costMatrix[i][j] += 10.0f;
			}
			if (measurements->at(i).src_blob.btype == BLOB_TYPE_FRONT &&
			    match_measurements->at(j).src_blob.btype == BLOB_TYPE_SIDE) {
				costMatrix[i][j] += 10.0f;
			}
		}
	}

	double cost = HungAlgo.Solve(costMatrix, assignment);
	(void)cost;

	for (uint32_t i = 0; i < measurements->size(); i++) {
		measurements->at(i).vertex_index = assignment[i];
	}

	std::vector<proximity_data_t> proximity_data;
	for (uint32_t i = 0; i < measurements->size(); i++) {
		float lowest_distance = 65535.0;
		int closest_index = 0;
		(void)lowest_distance;
		(void)closest_index;

		proximity_data_t p;
		match_data_t measurement = measurements->at(i);

		p.position = measurement.position;
		p.vertex_index = measurement.vertex_index;
		p.lowest_distance = 0.0f;
		proximity_data.push_back(p);
	}

	if (proximity_data.size() > 0) {

		// use the IMU rotation and the measured points in
		// world space to compute a transform from model to world space.
		// use each measured led individually and average the resulting
		// positions

		std::vector<match_model_t> temp_measurement_list;
		for (uint32_t i = 0; i < proximity_data.size(); i++) {
			proximity_data_t p = proximity_data[i];
			Eigen::Vector4f model_vertex = t.model_vertices[p.vertex_index].position;
			Eigen::Vector4f measurement_vertex = p.position;
			Eigen::Vector4f measurement_offset = t.corrected_imu_rotation * model_vertex;
			Eigen::Affine3f translation(
			    Eigen::Translation3f((measurement_vertex - measurement_offset).head<3>()));
			Eigen::Matrix4f model_to_measurement = translation.matrix() * t.corrected_imu_rotation;
			match_model_t temp_measurement;
			for (uint32_t j = 0; j < PSVR_NUM_LEDS; j++) {
				match_data_t md;
				md.position = model_to_measurement * t.model_vertices[j].position;
				md.vertex_index = j;
				temp_measurement.measurements.push_back(md);
			}
			temp_measurement_list.push_back(temp_measurement);
		}

		for (uint32_t i = 0; i < PSVR_NUM_LEDS; i++) {
			match_data_t avg_data;
			avg_data.position = Eigen::Vector4f(0.0f, 0.0f, 0.0f, 1.0f);
			for (uint32_t j = 0; j < temp_measurement_list.size(); j++) {
				avg_data.position += temp_measurement_list[j].measurements[i].position;
			}
			avg_data.position /= float(temp_measurement_list.size());
			avg_data.vertex_index = i;
			solved->push_back(avg_data);
		}

		std::vector<match_data_t> _solved;
		Eigen::Matrix4f pose = solve_for_measurement(&t, solved, &_solved) * t.corrected_imu_rotation;
		t.last_pose = pose;
		return pose;
	}
	PSVR_INFO("LOST TRACKING - RETURNING LAST POSE");
	t.max_correction = PSVR_SLOW_CORRECTION;
	return t.last_pose;
}


static Eigen::Matrix4f
disambiguate(TrackerPSVR &t,
             std::vector<match_data_t> *measured_points,
             std::vector<match_data_t> *last_measurement,
             std::vector<match_data_t> *solved,
             uint32_t frame_no)
{

	// main disambiguation routine - if we have enough points, use
	// optical matching, otherwise solve with imu.

	// do our imu-based solve up front - we can  use this to compute a more
	// likely match (currently disabled)

	Eigen::Matrix4f imu_solved_pose =
	    solve_with_imu(t, measured_points, last_measurement, solved, PSVR_SEARCH_RADIUS);

	if (measured_points->size() < PSVR_OPTICAL_SOLVE_THRESH && last_measurement->size() > 0) {
		return imu_solved_pose;
	}

	if (measured_points->size() < 3) {
		return imu_solved_pose;
	}


	// optical matching.

	float lowest_error = 65535.0f;
	int32_t best_model = -1;
	uint32_t matched_vertex_indices[PSVR_NUM_LEDS];

	// we can early-out if we are 'close enough' to our last match model.
	// if we just hold the previous led configuration, this increases
	// performance and should cut down on jitter.
	if (t.last_optical_model > 0 && t.done_correction) {

		match_model_t m = t.matches[t.last_optical_model];
		for (uint32_t i = 0; i < measured_points->size(); i++) {
			measured_points->at(i).vertex_index = m.measurements.at(i).vertex_index;
		}
		Eigen::Matrix4f res = solve_for_measurement(&t, measured_points, solved);
		float diff = last_diff(t, solved, &t.last_vertices);
		if (diff < PSVR_HOLD_THRESH) {
			// U_LOG_D("diff from last: %f", diff);

			return res;
		}
	}



	for (uint32_t i = 0; i < t.matches.size(); i++) {
		match_model_t m = t.matches[i];
		float error_sum = 0.0f;
		float sign_diff = 0.0f;
		(void)sign_diff;

		// we have 2 measurements per vertex (distance and
		// angle) and we are comparing only the 'non-basis
		// vector' elements

		// fill in our 'proposed' vertex indices from the model
		// data (this will be overwritten once our best model is
		// selected
		for (uint32_t j = 0; j < measured_points->size(); j++) {
			measured_points->at(j).vertex_index = m.measurements.at(j).vertex_index;
		}

		bool ignore = false;

		// use the information we gathered on blob shapes to
		// reject matches that would not fit

		//@todo: use tags instead  of numeric vertex indices

		for (uint32_t j = 0; j < measured_points->size(); j++) {

			if (measured_points->at(j).src_blob.btype == BLOB_TYPE_FRONT &&
			    measured_points->at(j).vertex_index > 4) {
				error_sum += 50.0f;
			}

			if (measured_points->at(j).src_blob.btype == BLOB_TYPE_SIDE &&
			    measured_points->at(j).vertex_index < 5) {
				error_sum += 50.0f;
			}

			// if the distance is between a measured point
			// and its last-known position is significantly
			// different, discard this
			float dist = fabs(measured_points->at(j).distance - m.measurements.at(j).distance);
			if (dist > PSVR_DISAMBIG_REJECT_DIST) {
				error_sum += 50.0f;
			} else {
				error_sum += fabs(measured_points->at(j).distance - m.measurements.at(j).distance);
			}

			// if the angle is significantly different,
			// discard this
			float angdiff = fabs(measured_points->at(j).angle - m.measurements.at(j).angle);
			if (angdiff > PSVR_DISAMBIG_REJECT_ANG) {
				error_sum += 50.0f;
			} else {

				error_sum += fabs(measured_points->at(j).angle - m.measurements.at(j).angle);
			}
		}

		float avg_error = (error_sum / measured_points->size());
		if (error_sum < 50) {
			std::vector<match_data_t> meas_solved;
			solve_for_measurement(&t, measured_points, &meas_solved);
			float prev_diff = last_diff(t, &meas_solved, &t.last_vertices);
			float imu_diff = last_diff(t, &meas_solved, solved);

			Eigen::Vector4f tl_pos, tr_pos, bl_pos, br_pos;
			bool has_bl = false;
			bool has_br = false;
			bool has_tl = false;
			bool has_tr = false;

			for (uint32_t j = 0; j < meas_solved.size(); j++) {
				match_data_t *md = &meas_solved.at(j);
				if (md->vertex_index == TAG_BL) {
					bl_pos = md->position;
					has_bl = true;
				}
				if (md->vertex_index == TAG_BR) {
					br_pos = md->position;
					has_br = true;
				}
				if (md->vertex_index == TAG_TL) {
					tl_pos = md->position;
					has_tl = true;
				}
				if (md->vertex_index == TAG_TR) {
					tr_pos = md->position;
					has_tr = true;
				}
			}

			// reject any configuration where 'top' is below
			// 'bottom

			if (has_bl && has_tl && bl_pos.y() > tl_pos.y()) {
				// U_LOG_D("IGNORING BL > TL %f %f",
				// bl_pos.y(),
				//      br_pos.y());
				// ignore = true;
			}
			if (has_br && has_tr && br_pos.y() > tr_pos.y()) {
				// U_LOG_D("IGNORING TL > TR %f %f",
				// tl_pos.y(),
				//       tr_pos.y());
				// ignore = true;
			}

			// once we have a lock, bias the detected
			// configuration using the imu-solved result,
			// and the solve from the previous frame

			if (t.done_correction) {
				avg_error += prev_diff;
				avg_error += imu_diff;
			}

			// useful for debugging
			// U_LOG_D(
			//    "match %d dist to last: %f dist to imu: %f "
			//    "rmsError: %f squaredSum:%f %d",
			//    i, prev_diff, imu_diff, avg_error, error_sum,
			//    ignore);
		}
		if (avg_error <= lowest_error && !ignore) {
			lowest_error = avg_error;
			best_model = i;
			for (uint32_t i = 0; i < measured_points->size(); i++) {
				matched_vertex_indices[i] = measured_points->at(i).vertex_index;
			}
		}
	}

	// U_LOG_D("lowest_error %f", lowest_error);
	if (best_model == -1) {
		PSVR_INFO("COULD NOT MATCH MODEL!");
		return Eigen::Matrix4f().Identity();
	}

	t.last_optical_model = best_model;
	for (uint32_t i = 0; i < measured_points->size(); i++) {
		measured_points->at(i).vertex_index = matched_vertex_indices[i];
		cv::putText(
		    t.debug.rgb[0],
		    cv::format("%d %d", measured_points->at(i).vertex_index, measured_points->at(i).src_blob.btype),
		    measured_points->at(i).src_blob.lkp.pt, cv::FONT_HERSHEY_SIMPLEX, 1.0f, cv::Scalar(0, 255, 0));
	}

	t.last_pose = solve_for_measurement(&t, measured_points, solved);

	return t.last_pose;
}

static void
create_model(TrackerPSVR &t)
{
	// this is the model we use to match our measurements against.
	// these vertices came out of the blender prototype.

	// NOTE: this is not an accurate measurement of the PSVRs
	// physical dimensions, rather an approximate model that serves
	// to minimize the incidence of incorrect led matches.

	t.model_vertices[0] = {
	    0,
	    Eigen::Vector4f(-0.06502f, 0.04335f, 0.01861f, 1.0f),
	    TAG_BL,
	    true,
	};
	t.model_vertices[1] = {
	    1,
	    Eigen::Vector4f(0.06502f, 0.04335f, 0.01861f, 1.0f),
	    TAG_BR,
	    true,
	};
	t.model_vertices[2] = {
	    2,
	    Eigen::Vector4f(0.0f, 0.0f, 0.04533f, 1.0f),
	    TAG_C,
	    true,
	};
	t.model_vertices[3] = {
	    3,
	    Eigen::Vector4f(-0.06502f, -0.04335f, 0.01861f, 1.0f),
	    TAG_TL,
	    true,
	};
	t.model_vertices[4] = {
	    4,
	    Eigen::Vector4f(0.06502f, -0.04335f, 0.01861f, 1.0f),
	    TAG_TR,
	    true,
	};
	t.model_vertices[5] = {
	    5,
	    Eigen::Vector4f(-0.07802f, 0.0f, -0.02671f, 1.0f),
	    TAG_SL,
	    true,
	};
	t.model_vertices[6] = {
	    6,
	    Eigen::Vector4f(0.07802f, 0.0f, -0.02671f, 1.0f),
	    TAG_SR,
	    true,
	};
}

struct Helper
{
public:
	m_permutator mp = {};
	model_vertex_t vec[PSVR_NUM_LEDS] = {};
	uint32_t indices[PSVR_NUM_LEDS];


public:
	~Helper()
	{
		m_permutator_reset(&mp);
	}

	bool
	step(TrackerPSVR &t)
	{
		bool ret = m_permutator_step(&mp, &indices[0], PSVR_NUM_LEDS);
		if (!ret) {
			return false;
		}

		for (size_t i = 0; i < PSVR_NUM_LEDS; i++) {
			vec[i] = t.model_vertices[indices[i]];
		}

		return true;
	}
};

static void
create_match_list(TrackerPSVR &t)
{
	// create our permutation list for matching
	// compute the distance and angles between a reference
	// vector, constructed from the first two vertices in
	// the permutation.

	Helper mp = {};
	while (mp.step(t)) {
		match_model_t m;

		model_vertex_t ref_pt_a = mp.vec[0];
		model_vertex_t ref_pt_b = mp.vec[1];
		Eigen::Vector3f ref_vec3 = (ref_pt_b.position - ref_pt_a.position).head<3>();

		float normScale = dist_3d(ref_pt_a.position, ref_pt_b.position);

		match_data_t md;
		for (auto &&i : mp.vec) {
			Eigen::Vector3f point_vec3 = (i.position - ref_pt_a.position).head<3>();
			md.vertex_index = i.vertex_index;
			md.distance = dist_3d(i.position, ref_pt_a.position) / normScale;
			if (i.position.head<3>().dot(Eigen::Vector3f(0.0, 0.0, 1.0f)) < 0) {
				md.distance *= -1;
			}

			Eigen::Vector3f plane_norm = ref_vec3.cross(point_vec3).normalized();
			if (ref_pt_a.position != i.position) {

				if (plane_norm.normalized().z() > 0) {
					md.angle = -1 * acos((point_vec3).normalized().dot(ref_vec3.normalized()));
				} else {
					md.angle = acos(point_vec3.normalized().dot(ref_vec3.normalized()));
				}
			} else {
				md.angle = 0.0f;
			}
			// fix up any NaNs
			if (md.angle != md.angle) {
				md.angle = 0.0f;
			}
			if (md.distance != md.distance) {
				md.distance = 0.0f;
			}

			m.measurements.push_back(md);
		}

		if (match_possible(&m)) {
			t.matches.push_back(m);
		}
	}
}

static void
do_view(TrackerPSVR &t, View &view, cv::Mat &grey, cv::Mat &rgb)
{
	// Undistort and rectify the whole image.
	cv::remap(grey,                         // src
	          view.frame_undist_rectified,  // dst
	          view.undistort_rectify_map_x, // map1
	          view.undistort_rectify_map_y, // map2
	          cv::INTER_NEAREST,            // interpolation - LINEAR seems
	                                        // very slow on my setup
	          cv::BORDER_CONSTANT,          // borderMode
	          cv::Scalar(0, 0, 0));         // borderValue

	cv::threshold(view.frame_undist_rectified, // src
	              view.frame_undist_rectified, // dst
	              32.0,                        // thresh
	              255.0,                       // maxval
	              0);
	t.sbd->detect(view.frame_undist_rectified, // image
	              view.keypoints,              // keypoints
	              cv::noArray());              // mask

	// Debug is wanted, draw the keypoints.
	if (rgb.cols > 0) {
		cv::drawKeypoints(view.frame_undist_rectified,                // image
		                  view.keypoints,                             // keypoints
		                  rgb,                                        // outImage
		                  cv::Scalar(255, 0, 0),                      // color
		                  cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS); // flags
	}
}

typedef struct blob_data
{
	int tc_to_bc; // top center to bottom center
	int lc_to_rc; // left center to right center
	int tl_to_br; // top left to bottom right
	int bl_to_tr; // bottom left to top right
	int diff_a;
	int diff_b;
	bool ignore;
} blob_data_t;


static void
sample_line(cv::Mat &src, cv::Point2i start, cv::Point2i end, int *inside_length)
{
	// use bresenhams algorithm to sample the
	// pixels between two points in an image

	*inside_length = 0;
	int curr_x = start.x;
	int curr_y = start.y;

	int slope_x = start.x < end.x ? 1 : -1;
	int slope_y = start.y < end.y ? 1 : -1;

	int dx = end.x - start.x;
	int dy = -1 * abs(end.y - start.y);
	int e_xy = dx + dy; /* error value e_xy */

	while (1) {
		// sample our pixel and see if it is in the interior
		if (curr_x > 0 && curr_y > 0) {
			// cv is row, column
			uint8_t *val = src.ptr(curr_y, curr_x);

			// @todo: we are just counting pixels rather
			// than measuring length - bresenhams may introduce some
			// inaccuracy here.
			if (*val > 128) {
				(*inside_length) += 1;
			}
		}
		if (curr_x == end.x && curr_y == end.y) {
			break;
		}
		int err2 = 2 * e_xy;
		if (err2 >= dy) {
			e_xy += dy;
			curr_x += slope_x;
		}
		if (err2 <= dx) {
			e_xy += dx;
			curr_y += slope_y;
		}
	}
}

static void
blob_intersections(cv::Mat &src, cv::KeyPoint *kp, struct blob_data *bd)
{
	// compute the intersections in 4 'directions' between the
	// extents of the 'square' region we get from the opencv blob
	// detector

	// compute the difference between the 'axis pairs' - the
	// relative magnitude and signs of these diffs can differentiate
	// between front and side blobs, as we can only ever see one
	// 'side' blob at a time, and its orientation will be opposite
	// to the others

	int radius = kp->size / 2;
	cv::Rect2i sq_b(kp->pt.x - radius, kp->pt.y - radius, kp->size, kp->size);

	sample_line(src, cv::Point2i(sq_b.x, sq_b.y), cv::Point2i(sq_b.x + sq_b.width, sq_b.y + sq_b.height),
	            &bd->tl_to_br);
	sample_line(src, cv::Point2i(sq_b.x, sq_b.y + sq_b.height), cv::Point2i(sq_b.x + sq_b.width, sq_b.y),
	            &bd->bl_to_tr);

	sample_line(src, cv::Point2i(sq_b.x, sq_b.y + sq_b.height / 2),
	            cv::Point2i(sq_b.x + sq_b.width, sq_b.y + sq_b.height / 2), &bd->tc_to_bc);

	sample_line(src, cv::Point2i(sq_b.x + sq_b.width / 2, sq_b.y),
	            cv::Point2i(sq_b.x + sq_b.width / 2, sq_b.y + sq_b.height), &bd->lc_to_rc);

	bd->diff_a = bd->tl_to_br - bd->bl_to_tr;
	bd->diff_b = bd->tc_to_bc - bd->lc_to_rc;
	bd->ignore = false;
}

static void
tag_points(TrackerPSVR &t, std::vector<blob_data_t> *blob_datas)
{
	// determine the 'channel' horiz/vert or 45 deg offset - with
	// the highest signal - and calculate the lower bound below
	// which we will ignore the blob, as it is not sufficiently
	// 'long' to identify
	int channel_a_total = 0;
	int channel_b_total = 0;
	int channel_a_min = INT_MAX;
	int channel_b_min = INT_MAX;
	int channel_a_max = INT_MIN;
	int channel_b_max = INT_MIN;
	int channel_a_pos = 0;
	int channel_a_neg = 0;
	int channel_b_pos = 0;
	int channel_b_neg = 0;


	for (uint32_t i = 0; i < blob_datas->size(); i++) {
		blob_data_t b = blob_datas->at(i);
		channel_a_total += abs(b.diff_a);
		if (abs(b.diff_a) < channel_a_min) {
			channel_a_min = b.diff_a;
		}
		if (abs(b.diff_a) > channel_a_max) {
			channel_a_min = b.diff_a;
		}

		if (b.diff_a < 0) {
			channel_a_neg++;
		} else {
			channel_a_pos++;
		}

		if (b.diff_b < 0) {
			channel_b_neg++;
		} else {
			channel_b_pos++;
		}

		channel_b_total += abs(b.diff_b);
		if (abs(b.diff_b) < channel_b_min) {
			channel_b_min = b.diff_b;
		}
		if (abs(b.diff_b) > channel_b_max) {
			channel_b_min = b.diff_b;
		}
	}

	int side_count = 0;
	if (channel_a_total > channel_b_total) {
		// use channel a
		float channel_dev = (channel_a_total / float(blob_datas->size())) / 2.0f;
		int usable_count = 0;

		for (uint32_t i = 0; i < blob_datas->size(); i++) {
			if (abs(blob_datas->at(i).diff_a) > channel_dev) {
				usable_count++;
			} else {
				if (blob_datas->at(i).diff_a < 0) {
					channel_a_neg--;
					blob_datas->at(i).ignore = true;
				} else {
					channel_a_pos--;
					blob_datas->at(i).ignore = true;
				}
			}
		}


		if (usable_count > 2) {
			// we can now check the signs, and identify the
			// 'odd one out' as the side LED - if we have a
			// consensus of directions, we can identify them
			// all as 'front' LEDs.
			for (uint32_t i = 0; i < blob_datas->size(); i++) {
				if (!blob_datas->at(i).ignore) {
					if (channel_a_pos > channel_a_neg) {
						// we can tag all the
						// positive ones with
						// FRONT and all the
						// negative ones with
						// SIDE
						if (blob_datas->at(i).diff_a >= 0) {
							t.world_points[i].btype = BLOB_TYPE_FRONT;
						} else {
							t.world_points[i].btype = BLOB_TYPE_SIDE;
							side_count++;
						}

					} else {
						if (blob_datas->at(i).diff_a < 0) {
							t.world_points[i].btype = BLOB_TYPE_FRONT;
						} else {
							t.world_points[i].btype = BLOB_TYPE_SIDE;
							side_count++;
						}
					}
				}
			}
		}
	} else {
		// use channel b
		float channel_dev = (channel_b_total / float(blob_datas->size())) / 2.0f;
		int usable_count = 0;
		for (uint32_t i = 0; i < blob_datas->size(); i++) {
			if (abs(blob_datas->at(i).diff_b) > channel_dev) {
				usable_count++;
			} else {
				if (blob_datas->at(i).diff_b < 0) {
					channel_b_neg--;
					blob_datas->at(i).ignore = true;
				} else {
					channel_b_pos--;
					blob_datas->at(i).ignore = true;
				}
			}
		}

		if (usable_count > 2) {
			// we can now check the signs, and identify the
			// 'odd one out' as the side LED - if we have a
			// consensus of directions, we can identify them
			// all as 'front' LEDs.
			for (uint32_t i = 0; i < blob_datas->size(); i++) {
				if (blob_datas->at(i).ignore) {
					continue;
				}
				if (channel_b_pos > channel_b_neg) {
					// we can tag all the positive ones with
					// FRONT and all the egative ones with
					// SIDE
					if (blob_datas->at(i).diff_b >= 0) {
						t.world_points[i].btype = BLOB_TYPE_FRONT;
					} else {
						t.world_points[i].btype = BLOB_TYPE_SIDE;
						side_count++;
					}

				} else {
					if (blob_datas->at(i).diff_b < 0) {
						t.world_points[i].btype = BLOB_TYPE_FRONT;
					} else {
						t.world_points[i].btype = BLOB_TYPE_SIDE;
						side_count++;
					}
				}
			}
		}
	}

	if (side_count > 1) {
		PSVR_INFO("FOUND MULTIPLE SIDE LEDS. should never happen!");
		for (uint32_t i = 0; i < t.world_points.size(); i++) {
			t.world_points.at(i).btype = BLOB_TYPE_UNKNOWN;
		}
	}
}


static void
process(TrackerPSVR &t, struct xrt_frame *xf)
{
	// No frame supplied, early-out.
	if (xf == NULL) {
		return;
	}

	t.debug.refresh(xf);

	// compute a dt for our filter(s)
	//@todo - use a more precise measurement here
	float dt = xf->source_sequence - t.last_frame;
	if (dt > 10.0f) {
		dt = 1.0f;
	}

	std::vector<match_data_t> predicted_pose;
	filter_predict(&predicted_pose, t.track_filters, dt / 2.0f);


	model_vertex_t measured_pose[PSVR_NUM_LEDS];
	(void)measured_pose;

	// get our raw measurements

	t.view[0].keypoints.clear();
	t.view[1].keypoints.clear();
	t.l_blobs.clear();
	t.r_blobs.clear();
	t.world_points.clear();

	int cols = xf->width / 2;
	int rows = xf->height;
	int stride = xf->stride;

	cv::Mat l_grey(rows, cols, CV_8UC1, xf->data, stride);
	cv::Mat r_grey(rows, cols, CV_8UC1, xf->data + cols, stride);

	do_view(t, t.view[0], l_grey, t.debug.rgb[0]);
	do_view(t, t.view[1], r_grey, t.debug.rgb[1]);

	// if we wish to confirm our camera input contents, dump frames
	// to disk

	// cv::imwrite("/tmp/l_view.png", t.view[0].frame_undist_rectified);
	// cv::imwrite("/tmp/r_view.png", t.view[1].frame_undist_rectified);

	// do some basic matching to come up with likely
	// disparity-pairs.

	for (uint32_t i = 0; i < t.view[0].keypoints.size(); i++) {
		cv::KeyPoint l_blob = t.view[0].keypoints[i];
		int l_index = -1;
		int r_index = -1;

		for (uint32_t j = 0; j < t.view[1].keypoints.size(); j++) {
			float lowest_dist = 65535.0f;
			cv::KeyPoint r_blob = t.view[1].keypoints[j];
			// find closest point on same-ish scanline
			float xdiff = r_blob.pt.x - l_blob.pt.x;
			float ydiff = r_blob.pt.y - l_blob.pt.y;
			if ((ydiff < 3.0f) && (ydiff > -3.0f) && (abs(xdiff) < lowest_dist)) {
				lowest_dist = abs(xdiff);
				r_index = j;
				l_index = i;
			}
		}

		if (l_index > -1 && r_index > -1) {
			cv::KeyPoint lkp = t.view[0].keypoints.at(l_index);
			cv::KeyPoint rkp = t.view[1].keypoints.at(r_index);
			t.l_blobs.push_back(lkp);
			t.r_blobs.push_back(rkp);
			// U_LOG_D("2D coords: LX %f LY %f RX %f RY %f",
			// lkp.pt.x,
			//       lkp.pt.y, rkp.pt.x, rkp.pt.y);
		}
	}

	// Convert our 2d point + disparities into 3d points.
	std::vector<blob_data_t> blob_datas;

	if (t.l_blobs.size() > 0) {
		for (uint32_t i = 0; i < t.l_blobs.size(); i++) {
			float disp = t.r_blobs[i].pt.x - t.l_blobs[i].pt.x;
			cv::Vec4d xydw(t.l_blobs[i].pt.x, t.l_blobs[i].pt.y, disp, 1.0f);
			// Transform
			cv::Vec4d h_world = (cv::Matx44d)t.disparity_to_depth * xydw;

			// Divide by scale to get 3D vector from
			// homogeneous coordinate. we also invert x here
			blob_point_t bp;
			bp.p =
			    cv::Point3f(-h_world[0] / h_world[3], h_world[1] / h_world[3], (h_world[2] / h_world[3]));
			bp.lkp = t.l_blobs[i];
			bp.rkp = t.r_blobs[i];
			bp.btype = BLOB_TYPE_UNKNOWN;
			t.world_points.push_back(bp);

			// compute the shape data for each blob

			blob_data_t intersections;
			blob_intersections(t.view[0].frame_undist_rectified, &bp.lkp, &intersections);
			blob_datas.push_back(intersections);
		}
	}

	tag_points(t, &blob_datas);



	t.pruned_points.clear();
	t.merged_points.clear();

	// remove outliers from our measurement list
	remove_outliers(&t.world_points, &t.pruned_points, PSVR_OUTLIER_THRESH);

	// remove any points that are too close to be
	// treated as separate leds
	merge_close_points(&t.pruned_points, &t.merged_points, PSVR_MERGE_THRESH);


	// uncomment to debug 'overpruning' or other issues
	// that may be related to calibration scale
	PSVR_INFO("world points: %d pruned points: %d merged points %d", (uint32_t)t.world_points.size(),
	          (uint32_t)t.pruned_points.size(), (uint32_t)t.merged_points.size());


	// put our blob positions in a slightly more
	// useful data structure

	if (t.merged_points.size() > PSVR_NUM_LEDS) {
		PSVR_INFO("Too many blobs to be a PSVR! %d", (uint32_t)t.merged_points.size());
	} else {
		// convert our points to match data,
		// this tags our match_vertices with
		// everything we need to solve the pose.

		verts_to_measurement(&t.merged_points, &t.match_vertices);
	}

#ifdef PSVR_DUMP_FOR_OFFLINE_ANALYSIS
	// raw debug output for Blender algo development
	for (size_t i = 0; i < t.merged_points.size(); i++) {

		cv::Point3f unscaled = t.merged_points.at(i).p;


		fprintf(t.dump_file, "P,%" PRIu64 ",%f,%f,%f\n", xf->source_sequence, unscaled.x, unscaled.y,
		        unscaled.z);
	}
	fprintf(t.dump_file, "\n");
#endif


	// our primary solving technique - optical and
	// fallback to imu-based is handled in the
	// disambiguate function - solved will contain our
	// best estimate of the position of the model vertices
	// in world space, and model_center_transform will
	// contain the pose matrix
	std::vector<match_data_t> solved;
	Eigen::Matrix4f model_center_transform = disambiguate(t, &t.match_vertices, &predicted_pose, &solved, 0);


	// derive our optical rotation correction from the
	// pose transform

	Eigen::Matrix3f r = model_center_transform.block(0, 0, 3, 3);
	Eigen::Quaternionf rot(r);

	// we only do this if we are pretty confident we
	// will have a 'good' optical pose i.e. front-5
	// leds.
	if (t.merged_points.size() >= PSVR_OPTICAL_SOLVE_THRESH) {
		Eigen::Quaternionf correction =
		    rot * Eigen::Quaternionf(t.fusion.rot.w, t.fusion.rot.x, t.fusion.rot.y, t.fusion.rot.z).inverse();

		float correction_magnitude = t.target_optical_rotation_correction.angularDistance(correction);

		// for corrections subsequent to the
		// first, we never want to depart
		// massively from the imu rotation, as
		// such major adjustments are likely to
		// be erroneous.

		// uncomment to debug rotation correction convergence
		// issues

		PSVR_TRACE("Q1: %f %f %f %f Q2: %f %f %f %f", t.target_optical_rotation_correction.x(),
		           t.target_optical_rotation_correction.y(), t.target_optical_rotation_correction.z(),
		           t.target_optical_rotation_correction.w(), correction.x(), correction.y(), correction.z(),
		           correction.w());
		PSVR_TRACE("correction mag: %f avg %f", correction_magnitude, t.avg_optical_correction);

		// keep a running average of the last 10 corrections -
		// so we can apply the correction only when we are
		// relatively stable
		t.avg_optical_correction -= t.avg_optical_correction / 10.0f;
		t.avg_optical_correction += correction_magnitude / 10.0f;

		PSVR_DEBUG("optical solve %f", t.avg_optical_correction);


		// if we have not yet applied a 'converged' correction,
		// our best chance of 'locking on' is to apply whatever
		// correction we compute.
		if (!t.done_correction) {
			t.target_optical_rotation_correction = correction;
			PSVR_INFO("RECORRECTING");
		}

		// only correct when we are stable
		if (t.avg_optical_correction < PSVR_CORRECTION_THRESH) {
			t.target_optical_rotation_correction = correction;
			t.done_correction = true;
			PSVR_INFO("LOCKED");
			t.max_correction = PSVR_FAST_CORRECTION;
			t.bad_correction_count = 0;
		}
		if (t.avg_optical_correction > PSVR_BAD_CORRECTION_THRESH) {
			t.bad_correction_count++;
		}

		if (t.bad_correction_count > PSVR_MAX_BAD_CORR) {
			t.max_correction = PSVR_SLOW_CORRECTION;
			t.target_optical_rotation_correction =
			    t.target_optical_rotation_correction.slerp(t.max_correction, correction);
			t.bad_correction_count = 0;
			PSVR_INFO("TOO MANY BAD CORRECTIONS. DRIFTED?");
		}

		std::vector<match_data_t> resolved;
		for (uint32_t i = 0; i < solved.size(); i++) {
			resolved.push_back(solved[i]);
		}
		solved.clear();
		model_center_transform = solve_with_imu(t, &resolved, &predicted_pose, &solved, PSVR_SEARCH_RADIUS);
	}

	// move our applied correction towards the
	// target correction, rather than applying it
	// immediately to smooth things out.

	t.optical_rotation_correction =
	    t.optical_rotation_correction.slerp(t.max_correction, t.target_optical_rotation_correction);

#ifdef PSVR_DUMP_FOR_OFFLINE_ANALYSIS
	fprintf(t.dump_file, "\n");
	for (uint32_t i = 0; i < solved.size(); i++) {
		fprintf(t.dump_file, "S,%" PRIu64 ",%f,%f,%f\n", xf->source_sequence, solved[i].position.x(),
		        solved[i].position.y(), solved[i].position.z());
	}
	fprintf(t.dump_file, "\n");

	/*std::vector<match_data_t> alt_solved;
	Eigen::Matrix4f f_pose = solve_with_imu(
	    t, &t.match_vertices, &t.last_vertices, &alt_solved, 10.0f);

	for (uint32_t i = 0; i < alt_solved.size(); i++) {
	        fprintf(t.dump_file, "A,%" PRIu64 ",%f,%f,%f\n",
	                xf->source_sequence, alt_solved[i].position.x(),
	                alt_solved[i].position.y(),
	alt_solved[i].position.z());
	}
	fprintf(t.dump_file, "\n");*/

#endif

	// store our last vertices for continuity
	// matching
	t.last_vertices.clear();
	for (uint32_t i = 0; i < solved.size(); i++) {
		t.last_vertices.push_back(solved[i]);
	}

	if (t.last_vertices.size() > 0) {
		filter_update(&t.last_vertices, t.track_filters, dt / 1000.0f);
	}


	Eigen::Vector4f position = model_center_transform.col(3);
	pose_filter_update(&position, &t.pose_filter, dt);



	// NOTE: we will apply our rotation when we get imu
	// data - applying our calculated optical
	// correction at this time. We can update our
	// position now.
	Eigen::Vector4f filtered_pose;
	pose_filter_predict(&filtered_pose, &t.pose_filter, dt / 1000.0f);


	t.optical.pos.x = filtered_pose.x();
	t.optical.pos.y = filtered_pose.y();
	t.optical.pos.z = filtered_pose.z();

	t.last_frame = xf->source_sequence;

	t.debug.submit();

	xrt_frame_reference(&xf, NULL);
}

static void
run(TrackerPSVR &t)
{
	struct xrt_frame *frame = NULL;

	os_thread_helper_lock(&t.oth);

	while (os_thread_helper_is_running_locked(&t.oth)) {
		// No data
		if (!t.has_imu || t.frame == NULL) {
			os_thread_helper_wait_locked(&t.oth);
		}

		if (!os_thread_helper_is_running_locked(&t.oth)) {
			break;
		}

		// Take a reference on the current frame

		frame = t.frame;
		t.frame = NULL;

		// Unlock the mutex when we do the work.
		os_thread_helper_unlock(&t.oth);

		process(t, frame);

		// Have to lock it again.
		os_thread_helper_lock(&t.oth);
	}

	os_thread_helper_unlock(&t.oth);
}

static void
get_pose(TrackerPSVR &t, timepoint_ns when_ns, struct xrt_space_relation *out_relation)
{
	os_thread_helper_lock(&t.oth);

	// Don't do anything if we have stopped.
	if (!os_thread_helper_is_running_locked(&t.oth)) {
		os_thread_helper_unlock(&t.oth);
		return;
	}

	out_relation->pose.position = t.optical.pos;
	out_relation->pose.orientation = t.optical.rot;

	//! @todo assuming that orientation is actually
	//! currently tracked.
	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_POSITION_VALID_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);

	os_thread_helper_unlock(&t.oth);
}

static void
imu_data(TrackerPSVR &t, timepoint_ns timestamp_ns, struct xrt_tracking_sample *sample)
{
	os_thread_helper_lock(&t.oth);

	// Don't do anything if we have stopped.
	if (!os_thread_helper_is_running_locked(&t.oth)) {
		os_thread_helper_unlock(&t.oth);
		return;
	}
	if (t.last_imu != 0) {
		time_duration_ns delta_ns = timestamp_ns - t.last_imu;
		float dt = time_ns_to_s(delta_ns);
		// Super simple fusion.
		math_quat_integrate_velocity(&t.fusion.rot, &sample->gyro_rad_secs, dt, &t.fusion.rot);
	}

	// apply our optical correction to imu rotation
	// data

	Eigen::Quaternionf corrected_rot_q =
	    t.optical_rotation_correction *
	    Eigen::Quaternionf(t.fusion.rot.w, t.fusion.rot.x, t.fusion.rot.y, t.fusion.rot.z);

	Eigen::Matrix4f corrected_rot = Eigen::Matrix4f::Identity();
	corrected_rot.block(0, 0, 3, 3) = corrected_rot_q.toRotationMatrix();

	t.corrected_imu_rotation = corrected_rot;

	if (t.done_correction) {
		corrected_rot_q = t.axis_align_rot * corrected_rot_q;
	}

	t.optical.rot.x = corrected_rot_q.x();
	t.optical.rot.y = corrected_rot_q.y();
	t.optical.rot.z = corrected_rot_q.z();
	t.optical.rot.w = corrected_rot_q.w();

	t.last_imu = timestamp_ns;

#ifdef PSVR_DUMP_IMU_FOR_OFFLINE_ANALYSIS
	fprintf(t.dump_file, "I,%" PRIu64 ", %f,%f,%f,%f\n\n", timestamp_ns, t.fusion.rot.x, t.fusion.rot.y,
	        t.fusion.rot.z, t.fusion.rot.w);

	fprintf(t.dump_file, "C,%" PRIu64 ", %f,%f,%f,%f\n\n", timestamp_ns, corrected_rot_q.x(), corrected_rot_q.y(),
	        corrected_rot_q.z(), corrected_rot_q.w());
#endif


	os_thread_helper_unlock(&t.oth);
}

static void
frame(TrackerPSVR &t, struct xrt_frame *xf)
{
	os_thread_helper_lock(&t.oth);

	// Don't do anything if we have stopped.
	if (!os_thread_helper_is_running_locked(&t.oth)) {
		os_thread_helper_unlock(&t.oth);
		return;
	}

	xrt_frame_reference(&t.frame, xf);

	// Wake up the thread.
	os_thread_helper_signal_locked(&t.oth);

	os_thread_helper_unlock(&t.oth);
}

static void
break_apart(TrackerPSVR &t)
{
	os_thread_helper_stop(&t.oth);
}


/*
 *
 * C wrapper functions.
 *
 */

extern "C" void
t_psvr_push_imu(struct xrt_tracked_psvr *xtvr, timepoint_ns timestamp_ns, struct xrt_tracking_sample *sample)
{
	auto &t = *container_of(xtvr, TrackerPSVR, base);
	imu_data(t, timestamp_ns, sample);
}

extern "C" void
t_psvr_get_tracked_pose(struct xrt_tracked_psvr *xtvr, timepoint_ns when_ns, struct xrt_space_relation *out_relation)
{
	auto &t = *container_of(xtvr, TrackerPSVR, base);
	get_pose(t, when_ns, out_relation);
}

extern "C" void
t_psvr_fake_destroy(struct xrt_tracked_psvr *xtvr)
{
	auto &t = *container_of(xtvr, TrackerPSVR, base);
	(void)t;
	// Not the real destroy function
}

extern "C" void
t_psvr_sink_push_frame(struct xrt_frame_sink *xsink, struct xrt_frame *xf)
{
	auto &t = *container_of(xsink, TrackerPSVR, sink);
	frame(t, xf);
}

extern "C" void
t_psvr_node_break_apart(struct xrt_frame_node *node)
{
	auto &t = *container_of(node, TrackerPSVR, node);
	break_apart(t);
}

extern "C" void
t_psvr_node_destroy(struct xrt_frame_node *node)
{
	auto t_ptr = container_of(node, TrackerPSVR, node);

	os_thread_helper_destroy(&t_ptr->oth);

	delete t_ptr;
}

extern "C" void *
t_psvr_run(void *ptr)
{
	auto &t = *(TrackerPSVR *)ptr;
	run(t);
	return NULL;
}


/*
 *
 * Exported functions.
 *
 */

extern "C" int
t_psvr_start(struct xrt_tracked_psvr *xtvr)
{
	auto &t = *container_of(xtvr, TrackerPSVR, base);
	int ret;


	ret = os_thread_helper_start(&t.oth, t_psvr_run, &t);
	if (ret != 0) {
		return ret;
	}

	return ret;
}

extern "C" int
t_psvr_create(struct xrt_frame_context *xfctx,
              struct t_stereo_camera_calibration *data,
              struct xrt_tracked_psvr **out_xtvr,
              struct xrt_frame_sink **out_sink)
{
	auto &t = *(new TrackerPSVR());
	t.ll = debug_get_log_option_psvr_log();

	PSVR_INFO("%s", __func__);
	int ret;

	for (uint32_t i = 0; i < PSVR_NUM_LEDS; i++) {
		init_filter(t.track_filters[i], PSVR_BLOB_PROCESS_NOISE, PSVR_BLOB_MEASUREMENT_NOISE, 1.0f);
	}

	init_filter(t.pose_filter, PSVR_POSE_PROCESS_NOISE, PSVR_POSE_MEASUREMENT_NOISE, 1.0f);

	StereoRectificationMaps rectify(data);
	t.view[0].populate_from_calib(data->view[0], rectify.view[0].rectify);
	t.view[1].populate_from_calib(data->view[1], rectify.view[1].rectify);
	t.disparity_to_depth = rectify.disparity_to_depth_mat;
	StereoCameraCalibrationWrapper wrapped(data);
	t.r_cam_rotation = wrapped.camera_rotation_mat;
	t.r_cam_translation = wrapped.camera_translation_mat;
	t.calibrated = true;



	// clang-format off
	cv::SimpleBlobDetector::Params blob_params;
	blob_params.filterByArea = false;
	blob_params.filterByConvexity = false;
	blob_params.filterByInertia = false;
	blob_params.filterByColor = true;
	blob_params.blobColor = 255; // 0 or 255 - color comes from binarized image?
	blob_params.minArea = 0;
	blob_params.maxArea = 1000;
	blob_params.maxThreshold = 51; // using a wide threshold span slows things down bigtime
	blob_params.minThreshold = 50;
	blob_params.thresholdStep = 1;
	blob_params.minDistBetweenBlobs = 5;
	blob_params.minRepeatability = 1; // need this to avoid error?
	// clang-format on

	t.sbd = cv::SimpleBlobDetector::create(blob_params);

	t.target_optical_rotation_correction = Eigen::Quaternionf(1.0f, 0.0f, 0.0f, 0.0f);
	t.optical_rotation_correction = Eigen::Quaternionf(1.0f, 0.0f, 0.0f, 0.0f);
	t.axis_align_rot = Eigen::Quaternionf(1.0f, 0.0f, 0.0f, 0.0f);
	t.corrected_imu_rotation = Eigen::Matrix4f().Identity();
	t.avg_optical_correction = 10.0f; // initialise to a high value, so we
	                                  // can converge to a low one.
	t.max_correction = PSVR_FAST_CORRECTION;
	t.bad_correction_count = 0;

	Eigen::Quaternionf align(Eigen::AngleAxis<float>(-M_PI / 2, Eigen::Vector3f(0.0f, 0.0f, 1.0f)));
	Eigen::Quaternionf align2(Eigen::AngleAxis<float>(M_PI, Eigen::Vector3f(0.0f, 1.0f, 0.0f)));

	t.axis_align_rot = align2; // * align;

	t.last_optical_model = 0;

	// offset our models center of rotation
	create_model(t);
	create_match_list(t);

	t.base.get_tracked_pose = t_psvr_get_tracked_pose;
	t.base.push_imu = t_psvr_push_imu;
	t.base.destroy = t_psvr_fake_destroy;
	t.sink.push_frame = t_psvr_sink_push_frame;
	t.node.break_apart = t_psvr_node_break_apart;
	t.node.destroy = t_psvr_node_destroy;
	t.fusion.rot.w = 1.0f;

	ret = os_thread_helper_init(&t.oth);
	if (ret != 0) {
		delete (&t);
		return ret;
	}

	t.fusion.pos.x = 0.0f;
	t.fusion.pos.y = 0.0f;
	t.fusion.pos.z = 0.0f;

	t.fusion.rot.x = 0.0f;
	t.fusion.rot.y = 0.0f;
	t.fusion.rot.z = 0.0f;
	t.fusion.rot.w = 1.0f;

	xrt_frame_context_add(xfctx, &t.node);

	// Everything is safe, now setup the variable tracking.
	u_var_add_root(&t, "PSVR Tracker", true);
	u_var_add_log_level(&t, &t.ll, "Log level");
	u_var_add_sink(&t, &t.debug.sink, "Debug");

	*out_sink = &t.sink;
	*out_xtvr = &t.base;

#ifdef PSVR_DUMP_FOR_OFFLINE_ANALYSIS
	t.dump_file = fopen("/tmp/psvr_dump.txt", "w");
#endif

	return 0;
}
