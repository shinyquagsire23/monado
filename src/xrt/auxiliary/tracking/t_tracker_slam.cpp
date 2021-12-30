// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief SLAM tracking code.
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup aux_tracking
 */

#include "xrt/xrt_config_have.h"
#include "xrt/xrt_tracking.h"
#include "xrt/xrt_frameserver.h"
#include "util/u_debug.h"
#include "util/u_var.h"
#include "os/os_threading.h"
#include "math/m_filter_fifo.h"
#include "math/m_filter_one_euro.h"
#include "math/m_predict.h"
#include "math/m_relation_history.h"
#include "math/m_space.h"
#include "math/m_vec3.h"
#include "tracking/t_euroc_recorder.h"

#include <slam_tracker.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/version.hpp>

#define SLAM_TRACE(...) U_LOG_IFL_T(t.log_level, __VA_ARGS__)
#define SLAM_DEBUG(...) U_LOG_IFL_D(t.log_level, __VA_ARGS__)
#define SLAM_INFO(...) U_LOG_IFL_I(t.log_level, __VA_ARGS__)
#define SLAM_WARN(...) U_LOG_IFL_W(t.log_level, __VA_ARGS__)
#define SLAM_ERROR(...) U_LOG_IFL_E(t.log_level, __VA_ARGS__)
#define SLAM_ASSERT(predicate, ...)                                                                                    \
	do {                                                                                                           \
		bool p = predicate;                                                                                    \
		if (!p) {                                                                                              \
			U_LOG(U_LOGGING_ERROR, __VA_ARGS__);                                                           \
			assert(false && "SLAM_ASSERT failed: " #predicate);                                            \
			exit(EXIT_FAILURE);                                                                            \
		}                                                                                                      \
	} while (false);
#define SLAM_ASSERT_(predicate) SLAM_ASSERT(predicate, "Assertion failed " #predicate)

// Debug assertions, not vital but useful for finding errors
#ifdef NDEBUG
#define SLAM_DASSERT(predicate, ...)
#define SLAM_DASSERT_(predicate)
#else
#define SLAM_DASSERT(predicate, ...) SLAM_ASSERT(predicate, __VA_ARGS__)
#define SLAM_DASSERT_(predicate) SLAM_ASSERT_(predicate)
#endif

//! SLAM tracking logging level
DEBUG_GET_ONCE_LOG_OPTION(slam_log, "SLAM_LOG", U_LOGGING_WARN)

//! Config file path, format is specific to the SLAM implementation in use
DEBUG_GET_ONCE_OPTION(slam_config, "SLAM_CONFIG", NULL)

//! Whether to submit data to the SLAM tracker without user action
DEBUG_GET_ONCE_BOOL_OPTION(slam_submit_from_start, "SLAM_SUBMIT_FROM_START", true)


//! Namespace for the interface to the external SLAM tracking system
namespace xrt::auxiliary::tracking::slam {
using xrt::auxiliary::math::RelationHistory;

using cv::Mat;
using cv::MatAllocator;
using cv::UMatData;
using cv::UMatUsageFlags;

#define USING_OPENCV_3_3_1 (CV_VERSION_MAJOR == 3 && CV_VERSION_MINOR == 3 && CV_VERSION_REVISION == 1)

#if defined(XRT_HAVE_KIMERA_SLAM) && !USING_OPENCV_3_3_1
#pragma message "Kimera-VIO uses OpenCV 3.3.1, use that to prevent conflicts"
#endif

//! @todo These defs should make OpenCV 4 work but it wasn't tested against a
//! SLAM system that supports that version yet.
#if CV_VERSION_MAJOR < 4
#define ACCESS_RW 0
typedef int AccessFlag;
#define CV_AUTOSTEP 0x7fffffff // From opencv2/core/core_c.h
#else
using cv::ACCESS_RW;
using cv::AccessFlag;
#define CV_AUTOSTEP cv::Mat::AUTO_STEP;
#endif

/*!
 * @brief Wraps a @ref xrt_frame with a `cv::Mat` (conversely to @ref FrameMat).
 *
 * It works by implementing a `cv::MatAllocator` which determines what to do
 * when a `cv::Mat` refcount reaches zero. In that case, it decrements the @ref
 * xrt_frame refcount once the `cv::Mat` own refcount has reached zero.
 *
 * @note a @ref MatFrame `cv::Mat` can wrap a @ref FrameMat @ref xrt_frame,
 * which in turns wraps a `cv::Mat`, with little overhead, and that is precisely
 * how it is being used in this file when the @ref xrt_frame is a @ref FrameMat.
 */
class MatFrame final : public MatAllocator
{
public:
	//! Wraps a @ref xrt_frame in a `cv::Mat`
	Mat
	wrap(struct xrt_frame *frame)
	{
		SLAM_DASSERT_(frame->format == XRT_FORMAT_L8 || frame->format == XRT_FORMAT_R8G8B8);
		auto img_type = frame->format == XRT_FORMAT_L8 ? CV_8UC1 : CV_8UC3;

		// Wrap the frame data into a cv::Mat header
		cv::Mat img{(int)frame->height, (int)frame->width, img_type, frame->data, frame->stride};

		// Enable reference counting for a user-allocated cv::Mat (i.e., using existing frame->data)
		img.u = this->allocate(img.dims, img.size.p, img.type(), img.data, img.step.p, ACCESS_RW,
		                       cv::USAGE_DEFAULT);
		SLAM_DASSERT_(img.u->refcount == 0);
		img.addref();

		// Keep a reference to the xrt_frame in the cv userdata field for when the cv::Mat reference reaches 0
		SLAM_DASSERT_(img.u->userdata == NULL); // Should be default-constructed
		xrt_frame_reference((struct xrt_frame **)&img.u->userdata, frame);

		return img;
	}

	//! Allocates a `cv::UMatData` object which is in charge of reference counting for a `cv::Mat`
	UMatData *
	allocate(
	    int dims, const int *sizes, int type, void *data0, size_t *step, AccessFlag, UMatUsageFlags) const override
	{
		SLAM_DASSERT_(dims == 2 && sizes && data0 && step && step[0] != CV_AUTOSTEP);
		UMatData *u = new UMatData(this);
		uchar *data = (uchar *)data0;
		u->data = u->origdata = data;
		u->size = step[0] * sizes[0];         // Row stride * row count
		u->flags |= UMatData::USER_ALLOCATED; // External data
		return u;
	}

	//! Necessary but unused virtual method for a `cv::MatAllocator`
	bool
	allocate(UMatData *, AccessFlag, UMatUsageFlags) const override
	{
		SLAM_ASSERT(false, "Shouldn't be reached");
		return false;
	}

	//! When `cv::UMatData` refcount reaches zero this method is called, we just
	//! decrement the original @ref xrt_frame refcount as it is the one in charge
	//! of the memory.
	void
	deallocate(UMatData *u) const override
	{
		SLAM_DASSERT_(u->urefcount == 0 && u->refcount == 0);
		SLAM_DASSERT_(u->flags & UMatData::USER_ALLOCATED);
		xrt_frame_reference((struct xrt_frame **)&u->userdata, NULL);
		delete u;
	}
};

//! SLAM prediction type. Naming scheme as follows:
//! P: position, O: orientation, A: angular velocity, L: linear velocity
//! S: From SLAM poses (slow, precise), I: From IMU data (fast, noisy)
enum prediction_type
{
	PREDICTION_NONE = 0,    //!< No prediction, always return the last SLAM tracked pose
	PREDICTION_SP_SO_SA_SL, //!< Predicts from last two SLAM poses only
	PREDICTION_SP_SO_IA_SL, //!< Predicts from last SLAM pose with angular velocity computed from IMU
	PREDICTION_SP_SO_IA_IL, //!< Predicts from last SLAM pose with angular and linear velocity computed from IMU
	PREDICTION_COUNT,
};

/*!
 * Main implementation of @ref xrt_tracked_slam. This is an adapter class for
 * SLAM tracking that wraps an external SLAM implementation.
 *
 * @implements xrt_tracked_slam
 * @implements xrt_frame_node
 * @implements xrt_frame_sink
 * @implements xrt_imu_sink
 */
struct TrackerSlam
{
	struct xrt_tracked_slam base = {};
	struct xrt_frame_node node = {}; //!< Will be called on destruction
	slam_tracker *slam;              //!< Pointer to the external SLAM system implementation

	struct xrt_slam_sinks sinks = {};      //!< Pointers to the sinks below
	struct xrt_frame_sink left_sink = {};  //!< Sends left camera frames to the SLAM system
	struct xrt_frame_sink right_sink = {}; //!< Sends right camera frames to the SLAM system
	struct xrt_imu_sink imu_sink = {};     //!< Sends imu samples to the SLAM system
	bool submit;                           //!< Whether to submit data pushed to sinks to the SLAM tracker

	enum u_logging_level log_level; //!< Logging level for the SLAM tracker, set by SLAM_LOG var
	struct os_thread_helper oth;    //!< Thread where the external SLAM system runs
	MatFrame *cv_wrapper;           //!< Wraps a xrt_frame in a cv::Mat to send to the SLAM system

	struct xrt_slam_sinks *euroc_recorder; //!< EuRoC dataset recording sinks
	bool euroc_record;                     //!< When true, samples will be saved to disk in EuRoC format

	// Used mainly for checking that the timestamps come in order
	timepoint_ns last_imu_ts = INT64_MIN;   //!< Last received IMU sample timestamp
	timepoint_ns last_left_ts = INT64_MIN;  //!< Last received left image timestamp
	timepoint_ns last_right_ts = INT64_MIN; //!< Last received right image timestamp

	// Prediction

	//!< Type of prediction to use
	prediction_type pred_type = PREDICTION_SP_SO_IA_SL;
	u_var_combo pred_combo;         //!< UI combo box to select @ref pred_type
	RelationHistory slam_rels{};    //!< A history of relations produced purely from external SLAM tracker data
	struct m_ff_vec3_f32 *gyro_ff;  //!< Last gyroscope samples
	struct m_ff_vec3_f32 *accel_ff; //!< Last accelerometer samples

	//! Used to correct accelerometer measurements when integrating into the prediction.
	//! @todo Should be automatically computed instead of required to be filled manually through the UI.
	xrt_vec3 gravity_correction{0, 0, -MATH_GRAVITY_M_S2};

	//! Filters are used to smooth out the resulting trajectory
	struct
	{
		// Moving average filter
		bool use_moving_average_filter = false;
		//! Time window in ms take the average on.
		//! Increasing it smooths out the tracking at the cost of adding delay.
		double window = 66;
		struct m_ff_vec3_f32 *pos_ff; //! Predicted positions fifo
		struct m_ff_vec3_f32 *rot_ff; //! Predicted rotations fifo (only xyz components, w is inferred)

		// Exponential smoothing filter
		bool use_exponential_smoothing_filter = false;
		float alpha = 0.1; //!< How much should we lerp towards the @ref target value on each update
		struct xrt_space_relation last = XRT_SPACE_RELATION_ZERO;   //!< Last filtered relation
		struct xrt_space_relation target = XRT_SPACE_RELATION_ZERO; //!< Target relation

		// One euro filter
		bool use_one_euro_filter = false;
		m_filter_euro_vec3 pos_oe;     //!< One euro position filter
		m_filter_euro_quat rot_oe;     //!< One euro rotation filter
		const float min_cutoff = M_PI; //!< Default minimum cutoff frequency
		const float min_dcutoff = 1;   //!< Default minimum cutoff frequency for the derivative
		const float beta = 1;          //!< Default speed coefficient

	} filter;
};

//! Dequeue all tracked poses from the SLAM system and update prediction data with them.
static bool
flush_poses(TrackerSlam &t)
{
	pose tracked_pose{};
	bool got_one = t.slam->try_dequeue_pose(tracked_pose);

	bool dequeued = got_one;
	while (dequeued) {
		// New pose
		pose np = tracked_pose;
		int64_t nts = np.timestamp;
		xrt_vec3 npos{np.px, np.py, np.pz};
		xrt_quat nrot{np.rx, np.ry, np.rz, np.rw};

		// Last relation
		xrt_space_relation lr = XRT_SPACE_RELATION_ZERO;
		uint64_t lts;
		t.slam_rels.get_latest(&lts, &lr);
		xrt_vec3 lpos = lr.pose.position;
		xrt_quat lrot = lr.pose.orientation;

		double dt = time_ns_to_s(nts - lts);

		SLAM_TRACE("Dequeued SLAM pose ts=%ld p=[%f,%f,%f] r=[%f,%f,%f,%f]", //
		           nts, np.px, np.py, np.pz, np.rx, np.ry, np.rz, np.rw);

		// Compute new relation based on new pose and velocities since last pose
		xrt_space_relation rel{};
		rel.relation_flags = XRT_SPACE_RELATION_BITMASK_ALL;
		rel.pose = {nrot, npos};
		rel.linear_velocity = (npos - lpos) / dt;
		math_quat_finite_difference(&lrot, &nrot, dt, &rel.angular_velocity);

		t.slam_rels.push(rel, nts);
		dequeued = t.slam->try_dequeue_pose(tracked_pose);
	}

	if (!got_one) {
		SLAM_TRACE("No poses to flush");
	}

	return got_one;
}

//! Return our best guess of the relation at time @p when_ns using all the data the tracker has.
static void
predict_pose(TrackerSlam &t, timepoint_ns when_ns, struct xrt_space_relation *out_relation)
{
	bool valid_pred_type = t.pred_type >= PREDICTION_NONE && t.pred_type <= PREDICTION_SP_SO_IA_IL;
	SLAM_DASSERT(valid_pred_type, "Invalid prediction type (%d)", t.pred_type);

	// Get last relation computed purely from SLAM data
	xrt_space_relation rel{};
	uint64_t rel_ts;
	bool empty = !t.slam_rels.get_latest(&rel_ts, &rel);

	// Stop if there is no previous relation to use for prediction
	if (empty) {
		out_relation->relation_flags = XRT_SPACE_RELATION_BITMASK_NONE;
		return;
	}

	// Use only last SLAM pose without prediction if PREDICTION_NONE
	if (t.pred_type == PREDICTION_NONE) {
		*out_relation = rel;
		return;
	}

	// Use only SLAM data if asking for an old point in time or PREDICTION_SP_SO_SA_SL
	SLAM_DASSERT_(rel_ts < INT64_MAX);
	if (t.pred_type == PREDICTION_SP_SO_SA_SL || when_ns <= (int64_t)rel_ts) {
		t.slam_rels.get(when_ns, out_relation);
		return;
	}

	// Update angular velocity with gyro data
	if (t.pred_type >= PREDICTION_SP_SO_IA_SL) {
		xrt_vec3 avg_gyro{};
		m_ff_vec3_f32_filter(t.gyro_ff, rel_ts, when_ns, &avg_gyro);
		math_quat_rotate_derivative(&rel.pose.orientation, &avg_gyro, &rel.angular_velocity);
	}

	// Update linear velocity with accel data
	if (t.pred_type >= PREDICTION_SP_SO_IA_IL) {
		xrt_vec3 avg_accel{};
		m_ff_vec3_f32_filter(t.accel_ff, rel_ts, when_ns, &avg_accel);
		xrt_vec3 world_accel{};
		math_quat_rotate_vec3(&rel.pose.orientation, &avg_accel, &world_accel);
		world_accel += t.gravity_correction;
		double slam_to_imu_dt = time_ns_to_s(t.last_imu_ts - rel_ts);
		rel.linear_velocity += world_accel * slam_to_imu_dt;
	}

	// Do the prediction based on the updated relation
	double slam_to_now_dt = time_ns_to_s(when_ns - rel_ts);
	xrt_space_relation predicted_relation{};
	m_predict_relation(&rel, slam_to_now_dt, &predicted_relation);

	*out_relation = predicted_relation;
}

//! Various filters to remove noise from the predicted trajectory.
static void
filter_pose(TrackerSlam &t, timepoint_ns when_ns, struct xrt_space_relation *out_relation)
{
	if (t.filter.use_moving_average_filter) {
		if (out_relation->relation_flags & XRT_SPACE_RELATION_POSITION_VALID_BIT) {
			xrt_vec3 pos = out_relation->pose.position;
			m_ff_vec3_f32_push(t.filter.pos_ff, &pos, when_ns);
		}

		if (out_relation->relation_flags & XRT_SPACE_RELATION_ORIENTATION_VALID_BIT) {
			// Don't save w component as we can retrieve it knowing these are unit quaternions
			xrt_vec3 rot = {out_relation->pose.orientation.x, out_relation->pose.orientation.y,
			                out_relation->pose.orientation.z};
			m_ff_vec3_f32_push(t.filter.rot_ff, &rot, when_ns);
		}

		// Get averages in time window
		timepoint_ns window = t.filter.window * U_TIME_1MS_IN_NS;
		xrt_vec3 avg_pos;
		m_ff_vec3_f32_filter(t.filter.pos_ff, when_ns - window, when_ns, &avg_pos);
		xrt_vec3 avg_rot; // Naive but good enough rotation average
		m_ff_vec3_f32_filter(t.filter.rot_ff, when_ns - window, when_ns, &avg_rot);

		float avg_rot_w = sqrtf(1 - (avg_rot.x * avg_rot.x + avg_rot.y * avg_rot.y + avg_rot.z * avg_rot.z));
		out_relation->pose.orientation = xrt_quat{avg_rot.x, avg_rot.y, avg_rot.z, avg_rot_w};
		out_relation->pose.position = avg_pos;
	}

	if (t.filter.use_exponential_smoothing_filter) {
		xrt_space_relation &last = t.filter.last;
		xrt_space_relation &target = t.filter.target;
		target = *out_relation;
		m_space_relation_interpolate(&last, &target, t.filter.alpha, target.relation_flags, &last);
		*out_relation = last;
	}

	if (t.filter.use_one_euro_filter) {
		xrt_pose &p = out_relation->pose;
		m_filter_euro_vec3_run(&t.filter.pos_oe, when_ns, &p.position, &p.position);
		m_filter_euro_quat_run(&t.filter.rot_oe, when_ns, &p.orientation, &p.orientation);
	}
}

} // namespace xrt::auxiliary::tracking::slam

using namespace xrt::auxiliary::tracking::slam;

//! Get a filtered prediction from the SLAM tracked poses.
extern "C" void
t_slam_get_tracked_pose(struct xrt_tracked_slam *xts, timepoint_ns when_ns, struct xrt_space_relation *out_relation)
{
	auto &t = *container_of(xts, TrackerSlam, base);
	flush_poses(t);
	predict_pose(t, when_ns, out_relation);
	filter_pose(t, when_ns, out_relation);
}

//! Receive and send IMU samples to the external SLAM system
extern "C" void
t_slam_imu_sink_push(struct xrt_imu_sink *sink, struct xrt_imu_sample *s)
{
	auto &t = *container_of(sink, TrackerSlam, imu_sink);

	timepoint_ns ts = s->timestamp_ns;
	xrt_vec3_f64 a = s->accel_m_s2;
	xrt_vec3_f64 w = s->gyro_rad_secs;

	//! @todo There are many conversions like these between xrt and
	//! slam_tracker.hpp types. Implement a casting mechanism to avoid copies.
	imu_sample sample{ts, a.x, a.y, a.z, w.x, w.y, w.z};
	if (t.submit) {
		t.slam->push_imu_sample(sample);
	}
	SLAM_TRACE("imu t=%ld a=[%f,%f,%f] w=[%f,%f,%f]", ts, a.x, a.y, a.z, w.x, w.y, w.z);

	if (t.euroc_record) {
		xrt_sink_push_imu(t.euroc_recorder->imu, s);
	}

	struct xrt_vec3 gyro = {(float)w.x, (float)w.y, (float)w.z};
	struct xrt_vec3 accel = {(float)a.x, (float)a.y, (float)a.z};
	m_ff_vec3_f32_push(t.gyro_ff, &gyro, ts);
	m_ff_vec3_f32_push(t.accel_ff, &accel, ts);

	// Check monotonically increasing timestamps
	SLAM_DASSERT(ts > t.last_imu_ts, "Sample (%ld) is older than last (%ld)", ts, t.last_imu_ts)
	t.last_imu_ts = ts;
}

//! Push the frame to the external SLAM system
static void
push_frame(TrackerSlam &t, struct xrt_frame *frame, bool is_left)
{
	// Construct and send the image sample
	cv::Mat img = t.cv_wrapper->wrap(frame);
	SLAM_DASSERT_(frame->timestamp < INT64_MAX);
	img_sample sample{(int64_t)frame->timestamp, img, is_left};
	if (t.submit) {
		t.slam->push_frame(sample);
	}
	SLAM_TRACE("%s frame t=%lu", is_left ? " left" : "right", frame->timestamp);

	// Check monotonically increasing timestamps
	timepoint_ns &last_ts = is_left ? t.last_left_ts : t.last_right_ts;
	SLAM_DASSERT(sample.timestamp > last_ts, "Frame (%ld) is older than last (%ld)", sample.timestamp, last_ts);
	last_ts = sample.timestamp;
}

extern "C" void
t_slam_frame_sink_push_left(struct xrt_frame_sink *sink, struct xrt_frame *frame)
{
	auto &t = *container_of(sink, TrackerSlam, left_sink);
	push_frame(t, frame, true);

	if (t.euroc_record) {
		xrt_sink_push_frame(t.euroc_recorder->left, frame);
	}
}

extern "C" void
t_slam_frame_sink_push_right(struct xrt_frame_sink *sink, struct xrt_frame *frame)
{
	auto &t = *container_of(sink, TrackerSlam, right_sink);
	push_frame(t, frame, false);

	if (t.euroc_record) {
		xrt_sink_push_frame(t.euroc_recorder->right, frame);
	}
}

extern "C" void
t_slam_node_break_apart(struct xrt_frame_node *node)
{
	auto &t = *container_of(node, TrackerSlam, node);
	t.slam->finalize();
	t.slam->stop();
	os_thread_helper_stop(&t.oth);
	SLAM_DEBUG("SLAM tracker dismantled");
}

extern "C" void
t_slam_node_destroy(struct xrt_frame_node *node)
{
	auto t_ptr = container_of(node, TrackerSlam, node);
	auto &t = *t_ptr; // Needed by SLAM_DEBUG
	SLAM_DEBUG("Destroying SLAM tracker");
	os_thread_helper_destroy(&t_ptr->oth);
	u_var_remove_root(t_ptr);
	m_ff_vec3_f32_free(&t.gyro_ff);
	m_ff_vec3_f32_free(&t.accel_ff);
	m_ff_vec3_f32_free(&t.filter.pos_ff);
	m_ff_vec3_f32_free(&t.filter.rot_ff);
	delete t_ptr->slam;
	delete t_ptr->cv_wrapper;
	delete t_ptr;
}

//! Runs the external SLAM system in a separate thread
extern "C" void *
t_slam_run(void *ptr)
{
	auto &t = *(TrackerSlam *)ptr;
	SLAM_DEBUG("SLAM tracker starting");
	t.slam->start();
	return NULL;
}

//! Starts t_slam_run
extern "C" int
t_slam_start(struct xrt_tracked_slam *xts)
{
	auto &t = *container_of(xts, TrackerSlam, base);
	int ret = os_thread_helper_start(&t.oth, t_slam_run, &t);
	SLAM_ASSERT(ret == 0, "Unable to start thread");
	SLAM_DEBUG("SLAM tracker started");
	return ret;
}

extern "C" int
t_slam_create(struct xrt_frame_context *xfctx, struct xrt_tracked_slam **out_xts, struct xrt_slam_sinks **out_sink)
{
	enum u_logging_level log_level = debug_get_log_option_slam_log();

	// Check that the external SLAM system built is compatible
	int ima = IMPLEMENTATION_VERSION_MAJOR;
	int imi = IMPLEMENTATION_VERSION_MINOR;
	int ipa = IMPLEMENTATION_VERSION_PATCH;
	int hma = HEADER_VERSION_MAJOR;
	int hmi = HEADER_VERSION_MINOR;
	int hpa = HEADER_VERSION_PATCH;
	U_LOG_IFL_I(log_level, "External SLAM system built %d.%d.%d, expected %d.%d.%d.", ima, imi, ipa, hma, hmi, hpa);
	if (IMPLEMENTATION_VERSION_MAJOR != HEADER_VERSION_MAJOR) {
		U_LOG_IFL_E(log_level, "Incompatible external SLAM system found.");
		return -1;
	}
	U_LOG_IFL_I(log_level, "Initializing compatible external SLAM system.");

	// Check the user has provided a SLAM_CONFIG file
	const char *config_file = debug_get_option_slam_config();
	if (!config_file) {
		U_LOG_IFL_W(log_level,
		            "SLAM tracker requires a config file set with the SLAM_CONFIG environment variable");
		return -1;
	}

	auto &t = *(new TrackerSlam{});
	t.log_level = log_level;
	t.cv_wrapper = new MatFrame();

	t.base.get_tracked_pose = t_slam_get_tracked_pose;

	std::string config_file_string = std::string(config_file);
	t.slam = new slam_tracker{config_file_string};

	t.slam->initialize();

	t.left_sink.push_frame = t_slam_frame_sink_push_left;
	t.right_sink.push_frame = t_slam_frame_sink_push_right;
	t.imu_sink.push_imu = t_slam_imu_sink_push;

	t.sinks.left = &t.left_sink;
	t.sinks.right = &t.right_sink;
	t.sinks.imu = &t.imu_sink;

	t.submit = debug_get_bool_option_slam_submit_from_start();

	t.node.break_apart = t_slam_node_break_apart;
	t.node.destroy = t_slam_node_destroy;

	int ret = os_thread_helper_init(&t.oth);
	SLAM_ASSERT(ret == 0, "Unable to initialize thread");

	xrt_frame_context_add(xfctx, &t.node);

	t.euroc_recorder = euroc_recorder_create(xfctx, NULL);

	m_filter_euro_vec3_init(&t.filter.pos_oe, t.filter.min_cutoff, t.filter.beta, t.filter.min_dcutoff);
	m_filter_euro_quat_init(&t.filter.rot_oe, t.filter.min_cutoff, t.filter.beta, t.filter.min_dcutoff);

	// Setup UI
	t.pred_combo.count = PREDICTION_COUNT;
	t.pred_combo.options = "None\0SP SO SA SL\0SP SO IA SL\0SP SO IA IL\0\0";
	t.pred_combo.value = (int *)&t.pred_type;
	m_ff_vec3_f32_alloc(&t.gyro_ff, 1000);
	m_ff_vec3_f32_alloc(&t.accel_ff, 1000);
	m_ff_vec3_f32_alloc(&t.filter.pos_ff, 1000);
	m_ff_vec3_f32_alloc(&t.filter.rot_ff, 1000);

	u_var_add_root(&t, "SLAM Tracker", true);
	u_var_add_log_level(&t, &t.log_level, "Log Level");
	u_var_add_bool(&t, &t.submit, "Submit data to SLAM");
	u_var_add_bool(&t, &t.euroc_record, "Record as EuRoC");
	u_var_add_gui_header(&t, NULL, "Trajectory Filter");
	u_var_add_bool(&t, &t.filter.use_moving_average_filter, "Enable moving average filter");
	u_var_add_f64(&t, &t.filter.window, "Window size (ms)");
	u_var_add_bool(&t, &t.filter.use_exponential_smoothing_filter, "Enable exponential smoothing filter");
	u_var_add_f32(&t, &t.filter.alpha, "Smoothing factor");
	u_var_add_bool(&t, &t.filter.use_one_euro_filter, "Enable one euro filter");
	u_var_add_f32(&t, &t.filter.pos_oe.base.fc_min, "Position minimum cutoff");
	u_var_add_f32(&t, &t.filter.pos_oe.base.beta, "Position beta speed");
	u_var_add_f32(&t, &t.filter.pos_oe.base.fc_min_d, "Position minimum delta cutoff");
	u_var_add_f32(&t, &t.filter.rot_oe.base.fc_min, "Orientation minimum cutoff");
	u_var_add_f32(&t, &t.filter.rot_oe.base.beta, "Orientation beta speed");
	u_var_add_f32(&t, &t.filter.rot_oe.base.fc_min_d, "Orientation minimum delta cutoff");
	u_var_add_gui_header(&t, NULL, "Prediction");
	u_var_add_combo(&t, &t.pred_combo, "Prediction Type");
	u_var_add_ro_ff_vec3_f32(&t, t.gyro_ff, "Gyroscope");
	u_var_add_ro_ff_vec3_f32(&t, t.accel_ff, "Accelerometer");
	u_var_add_f32(&t, &t.gravity_correction.z, "Gravity Correction");

	*out_xts = &t.base;
	*out_sink = &t.sinks;

	SLAM_DEBUG("SLAM tracker created");
	return 0;
}
