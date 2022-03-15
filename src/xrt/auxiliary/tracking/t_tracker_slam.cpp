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

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>

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
#define SLAM_DASSERT(predicate, ...) (void)(predicate)
#define SLAM_DASSERT_(predicate) (void)(predicate)
#else
#define SLAM_DASSERT(predicate, ...) SLAM_ASSERT(predicate, __VA_ARGS__)
#define SLAM_DASSERT_(predicate) SLAM_ASSERT_(predicate)
#endif

//! SLAM tracking logging level
DEBUG_GET_ONCE_LOG_OPTION(slam_log, "SLAM_LOG", U_LOGGING_WARN)

//! Config file path, format is specific to the SLAM implementation in use
DEBUG_GET_OPTION(slam_config, "SLAM_CONFIG", NULL)

//! Whether to submit data to the SLAM tracker without user action
DEBUG_GET_ONCE_BOOL_OPTION(slam_submit_from_start, "SLAM_SUBMIT_FROM_START", false)

//! Which level of prediction to use, @see prediction_type.
DEBUG_GET_ONCE_NUM_OPTION(slam_prediction_type, "SLAM_PREDICTION_TYPE", 2)

//! Whether to enable CSV writers from the start for later analysis
DEBUG_GET_ONCE_BOOL_OPTION(slam_write_csvs, "SLAM_WRITE_CSVS", false)

//! Path to write CSVs to
DEBUG_GET_OPTION(slam_csv_path, "SLAM_CSV_PATH", "evaluation/")

//! Namespace for the interface to the external SLAM tracking system
namespace xrt::auxiliary::tracking::slam {
constexpr int UI_TIMING_POSE_COUNT = 192;
constexpr int UI_GTDIFF_POSE_COUNT = 192;

using std::ifstream;
using std::make_unique;
using std::map;
using std::ofstream;
using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::vector;
using std::filesystem::create_directories;
using Trajectory = map<timepoint_ns, xrt_pose>;

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
#define CV_AUTOSTEP cv::Mat::AUTO_STEP
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

//! Writes poses and their timestamps to a CSV file
class TrajectoryWriter
{
public:
	bool enabled; // Modified through UI

private:
	string filename;
	ofstream file;
	bool created = false;

	void
	create()
	{
		string csv_path = debug_get_option_slam_csv_path();
		create_directories(csv_path);
		file = ofstream{csv_path + "/" + filename};
		file << "#timestamp [ns], p_RS_R_x [m], p_RS_R_y [m], p_RS_R_z [m], "
		        "q_RS_w [], q_RS_x [], q_RS_y [], q_RS_z []" CSV_EOL;
		file << std::fixed << std::setprecision(CSV_PRECISION);
	}


public:
	TrajectoryWriter(const string &fn, bool e) : enabled(e), filename(fn) {}

	void
	push(timepoint_ns ts, const xrt_pose &pose)
	{
		if (!enabled) {
			return;
		}

		if (!created) {
			created = true;
			create();
		}

		xrt_vec3 p = pose.position;
		xrt_quat r = pose.orientation;
		file << ts << ",";
		file << p.x << "," << p.y << "," << p.z << ",";
		file << r.w << "," << r.x << "," << r.y << "," << r.z << CSV_EOL;
	}
};

//! Writes timestamps measured when estimating a new pose by the SLAM system
class TimingWriter
{
public:
	bool enabled; // Modified through UI

private:
	string filename;
	vector<string> column_names;
	ofstream file;
	bool created = false;

	void
	create()
	{
		string csv_path = debug_get_option_slam_csv_path();
		create_directories(csv_path);
		file = ofstream{csv_path + "/" + filename};
		file << "#";
		for (const string &col : column_names) {
			string delimiter = &col != &column_names.back() ? "," : CSV_EOL;
			file << col << delimiter;
		}
	}

public:
	TimingWriter(const string &fn, bool e, const vector<string> &cn) : enabled(e), filename(fn), column_names(cn) {}

	void
	push(const vector<timepoint_ns> &timestamps)
	{
		if (!enabled) {
			return;
		}

		if (!created) {
			created = true;
			create();
		}

		for (const timepoint_ns &ts : timestamps) {
			string delimiter = &ts != &timestamps.back() ? "," : CSV_EOL;
			file << ts << delimiter;
		}
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
 * @implements xrt_pose_sink
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
	struct xrt_pose_sink gt_sink = {};     //!< Register groundtruth trajectory for stats
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
	prediction_type pred_type;
	u_var_combo pred_combo;         //!< UI combo box to select @ref pred_type
	RelationHistory slam_rels{};    //!< A history of relations produced purely from external SLAM tracker data
	struct m_ff_vec3_f32 *gyro_ff;  //!< Last gyroscope samples
	struct m_ff_vec3_f32 *accel_ff; //!< Last accelerometer samples

	//! Used to correct accelerometer measurements when integrating into the prediction.
	//! @todo Should be automatically computed instead of required to be filled manually through the UI.
	xrt_vec3 gravity_correction{0, 0, -MATH_GRAVITY_M_S2};

	struct xrt_space_relation last_rel = XRT_SPACE_RELATION_ZERO; //!< Last reported/tracked pose
	timepoint_ns last_ts;                                         //!< Last reported/tracked pose timestamp

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
		const float beta = 0.16;       //!< Default speed coefficient

	} filter;

	// Stats and metrics

	// CSV writers for offline analysis (using pointers because of container_of)
	TimingWriter *slam_times_writer;    //!< Timestamps of the pipeline for performance analysis
	TrajectoryWriter *slam_traj_writer; //!< Estimated poses from the SLAM system
	TrajectoryWriter *pred_traj_writer; //!< Predicted poses
	TrajectoryWriter *filt_traj_writer; //!< Predicted and filtered poses

	//! Tracker timing info for performance evaluation
	struct
	{
		float dur_ms[UI_TIMING_POSE_COUNT]; //!< Timing durations in ms
		int idx = 0;                        //!< Index of latest entry in @ref dur_ms
		u_var_combo start_ts;               //!< UI combo box to select initial timing measurement
		u_var_combo end_ts;                 //!< UI combo box to select final timing measurement
		int start_ts_idx;                   //!< Selected initial timing measurement in @ref start_ts
		int end_ts_idx;                     //!< Selected final timing measurement in @ref end_ts
		struct u_var_timing ui;             //!< Realtime UI for tracker durations
		bool ext_enabled = false;           //!< Whether the SLAM system supports the timing extension
		vector<string> columns;             //!< Column names of the measured timestamps
		string joined_columns;              //!< Column names as a null separated string
	} timing;

	//! Ground truth related fields
	struct
	{
		Trajectory *trajectory;               //!< Empty if we've not received groundtruth
		xrt_pose origin;                      //!< First ground truth pose
		float diffs_mm[UI_GTDIFF_POSE_COUNT]; //!< Positional error wrt ground truth
		int diff_idx = 0;                     //!< Index of last error in @ref diffs_mm
		struct u_var_timing diff_ui;          //!< Realtime UI for positional error
		bool override_tracking = false;       //!< Force the tracker to report gt poses instead
	} gt;
};


/*
 *
 * Timing functionality
 *
 */

static void
timing_ui_setup(TrackerSlam &t)
{
	// Construct null-separated array of options for the combo box
	using namespace std::string_literals;
	t.timing.joined_columns = "";
	for (const string &name : t.timing.columns) {
		t.timing.joined_columns += name + "\0"s;
	}
	t.timing.joined_columns += "\0"s;

	t.timing.start_ts.count = t.timing.columns.size();
	t.timing.start_ts.options = t.timing.joined_columns.c_str();
	t.timing.start_ts.value = &t.timing.start_ts_idx;
	t.timing.start_ts_idx = 0;
	u_var_add_combo(&t, &t.timing.start_ts, "Start timestamp");

	t.timing.end_ts.count = t.timing.columns.size();
	t.timing.end_ts.options = t.timing.joined_columns.c_str();
	t.timing.end_ts.value = &t.timing.end_ts_idx;
	t.timing.end_ts_idx = t.timing.columns.size() - 1;
	u_var_add_combo(&t, &t.timing.end_ts, "End timestamp");

	t.timing.ui.values.data = t.timing.dur_ms;
	t.timing.ui.values.length = UI_TIMING_POSE_COUNT;
	t.timing.ui.values.index_ptr = &t.timing.idx;
	t.timing.ui.reference_timing = 16.6;
	t.timing.ui.center_reference_timing = true;
	t.timing.ui.range = t.timing.ui.reference_timing;
	t.timing.ui.dynamic_rescale = true;
	t.timing.ui.unit = "ms";
	u_var_add_f32_timing(&t, &t.timing.ui, "External tracker times");
}

//! Updates timing UI with info from a computed pose and returns that info
static vector<timepoint_ns>
timing_ui_push(TrackerSlam &t, const pose &p)
{
	timepoint_ns now = os_monotonic_get_ns();
	vector<timepoint_ns> tss = {p.timestamp, now};

	// Add extra timestamps if the SLAM tracker provides them
	if (t.timing.ext_enabled) {
		shared_ptr<pose_extension> ext = p.find_pose_extension(pose_ext_type::TIMING);
		SLAM_DASSERT(ext != nullptr, "An enabled extension was null");
		pose_ext_timing pet = *std::static_pointer_cast<pose_ext_timing>(ext);
		tss.insert(tss.begin() + 1, pet.timestamps.begin(), pet.timestamps.end());
	}

	// The two timestamps to compare in the graph
	timepoint_ns start = tss.at(t.timing.start_ts_idx);
	timepoint_ns end = tss.at(t.timing.end_ts_idx);

	// Push to the UI graph
	float tss_ms = (end - start) / U_TIME_1MS_IN_NS;
	t.timing.idx = (t.timing.idx + 1) % UI_TIMING_POSE_COUNT;
	t.timing.dur_ms[t.timing.idx] = tss_ms;
	constexpr float a = 1.0f / UI_TIMING_POSE_COUNT; // Exponential moving average
	t.timing.ui.reference_timing = (1 - a) * t.timing.ui.reference_timing + a * tss_ms;

	return tss;
}


/*
 *
 * Ground truth functionality
 *
 */

//! Gets an interpolated groundtruth pose (if available) at a specified timestamp
static xrt_pose
get_gt_pose_at(const Trajectory &gt, timepoint_ns ts)
{
	if (gt.empty()) {
		return XRT_POSE_IDENTITY;
	}

	Trajectory::const_iterator rit = gt.upper_bound(ts);

	if (rit == gt.begin()) { // Too far in the past, return first gt pose
		return gt.begin()->second;
	}

	if (rit == gt.end()) { // Too far in the future, return last gt pose
		return std::prev(gt.end())->second;
	}

	Trajectory::const_iterator lit = std::prev(rit);

	const auto &[lts, lpose] = *lit;
	const auto &[rts, rpose] = *rit;

	float t = double(ts - lts) / double(rts - lts);
	SLAM_DASSERT_(0 <= t && t <= 1);

	xrt_pose res{};
	math_quat_slerp(&lpose.orientation, &rpose.orientation, t, &res.orientation);
	res.position = m_vec3_lerp(lpose.position, rpose.position, t);
	return res;
}

//! Converts a pose from the tracker to ground truth
static struct xrt_pose
xr2gt_pose(const xrt_pose &gt_origin, const xrt_pose &xr_pose)
{
	//! @todo Right now this is hardcoded for Basalt and the EuRoC vicon datasets
	//! groundtruth and ignores orientation. Applies a fixed transformation so
	//! that the tracked and groundtruth trajectories origins and general motion
	//! match. The usual way of evaluating trajectory errors in SLAM requires to
	//! first align the trajectories through a non-linear optimization (e.g. gauss
	//! newton) so that they are as similar as possible. For this you need the
	//! entire tracked trajectory to be known beforehand, which makes it not
	//! suitable for reporting an error metric in realtime. See this 2-page paper
	//! for more info on trajectory alignment:
	//! https://ylatif.github.io/movingsensors/cameraReady/paper07.pdf

	xrt_vec3 pos = xr_pose.position;
	xrt_quat z180{0, 0, 1, 0};
	math_quat_rotate_vec3(&z180, &pos, &pos);
	math_quat_rotate_vec3(&gt_origin.orientation, &pos, &pos);
	pos += gt_origin.position;

	return {XRT_QUAT_IDENTITY, pos};
}

//! The inverse of @ref xr2gt_pose.
static struct xrt_pose
gt2xr_pose(const xrt_pose &gt_origin, const xrt_pose &gt_pose)
{
	xrt_vec3 pos = gt_pose.position;
	pos -= gt_origin.position;
	xrt_quat gt_origin_orientation_inv = gt_origin.orientation;
	math_quat_invert(&gt_origin_orientation_inv, &gt_origin_orientation_inv);
	math_quat_rotate_vec3(&gt_origin_orientation_inv, &pos, &pos);
	xrt_quat zn180{0, 0, -1, 0};
	math_quat_rotate_vec3(&zn180, &pos, &pos);

	return {XRT_QUAT_IDENTITY, pos};
}

static void
gt_ui_setup(TrackerSlam &t)
{
	t.gt.diff_ui.values.data = t.gt.diffs_mm;
	t.gt.diff_ui.values.length = UI_GTDIFF_POSE_COUNT;
	t.gt.diff_ui.values.index_ptr = &t.gt.diff_idx;
	t.gt.diff_ui.reference_timing = 0;
	t.gt.diff_ui.center_reference_timing = true;
	t.gt.diff_ui.range = 100; // 10cm
	t.gt.diff_ui.dynamic_rescale = true;
	t.gt.diff_ui.unit = "mm";
	u_var_add_f32_timing(&t, &t.gt.diff_ui, "Tracking absolute error");
}

static void
gt_ui_push(TrackerSlam &t, timepoint_ns ts, xrt_pose tracked_pose)
{
	if (t.gt.trajectory->empty()) {
		return;
	}

	xrt_pose gt_pose = get_gt_pose_at(*t.gt.trajectory, ts);
	xrt_pose xr_pose = xr2gt_pose(t.gt.origin, tracked_pose);

	float len_mm = m_vec3_len(xr_pose.position - gt_pose.position) * 1000;
	t.gt.diff_idx = (t.gt.diff_idx + 1) % UI_GTDIFF_POSE_COUNT;
	t.gt.diffs_mm[t.gt.diff_idx] = len_mm;
	constexpr float a = 1.0f / UI_GTDIFF_POSE_COUNT; // Exponential moving average
	t.gt.diff_ui.reference_timing = (1 - a) * t.gt.diff_ui.reference_timing + a * len_mm;
}

/*
 *
 * Tracker functionality
 *
 */

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

		gt_ui_push(t, nts, rel.pose);
		t.slam_traj_writer->push(nts, rel.pose);

		auto tss = timing_ui_push(t, np);
		t.slam_times_writer->push(tss);

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
			// Don't save w component as we can retrieve it knowing these are (almost) unit quaternions
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

		// Considering the naive averaging this W is a bit wrong, but it feels reasonably well
		float avg_rot_w = sqrtf(1 - (avg_rot.x * avg_rot.x + avg_rot.y * avg_rot.y + avg_rot.z * avg_rot.z));
		out_relation->pose.orientation = xrt_quat{avg_rot.x, avg_rot.y, avg_rot.z, avg_rot_w};
		out_relation->pose.position = avg_pos;

		//! @todo Implement the quaternion averaging with a m_ff_vec4_f32 and
		//! normalization. Although it would be best to have a way of generalizing
		//! types before so as to not have redundant copies of ff logic.
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
		if (out_relation->relation_flags & XRT_SPACE_RELATION_POSITION_VALID_BIT) {
			m_filter_euro_vec3_run(&t.filter.pos_oe, when_ns, &p.position, &p.position);
		}
		if (out_relation->relation_flags & XRT_SPACE_RELATION_ORIENTATION_VALID_BIT) {
			m_filter_euro_quat_run(&t.filter.rot_oe, when_ns, &p.orientation, &p.orientation);
		}
	}
}

static void
setup_ui(TrackerSlam &t)
{
	t.pred_combo.count = PREDICTION_COUNT;
	t.pred_combo.options = "None\0Interpolate SLAM poses\0Also gyro\0Also accel (needs gravity correction)\0\0";
	t.pred_combo.value = (int *)&t.pred_type;
	m_ff_vec3_f32_alloc(&t.gyro_ff, 1000);
	m_ff_vec3_f32_alloc(&t.accel_ff, 1000);
	m_ff_vec3_f32_alloc(&t.filter.pos_ff, 1000);
	m_ff_vec3_f32_alloc(&t.filter.rot_ff, 1000);

	u_var_add_root(&t, "SLAM Tracker", true);
	u_var_add_log_level(&t, &t.log_level, "Log Level");
	u_var_add_bool(&t, &t.submit, "Submit data to SLAM");
	u_var_add_bool(&t, &t.euroc_record, "Record as EuRoC");
	u_var_add_bool(&t, &t.gt.override_tracking, "Track with ground truth (if available)");

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

	u_var_add_gui_header(&t, NULL, "Stats");
	u_var_add_bool(&t, &t.slam_traj_writer->enabled, "Record tracked trajectory");
	u_var_add_bool(&t, &t.pred_traj_writer->enabled, "Record predicted trajectory");
	u_var_add_bool(&t, &t.filt_traj_writer->enabled, "Record filtered trajectory");
	u_var_add_bool(&t, &t.slam_times_writer->enabled, "Record tracker times");
	timing_ui_setup(t);
	// Later, gt_ui_setup will setup the tracking error UI if ground truth becomes available
}

} // namespace xrt::auxiliary::tracking::slam

using namespace xrt::auxiliary::tracking::slam;

/*
 *
 * External functions
 *
 */

//! Get a filtered prediction from the SLAM tracked poses.
extern "C" void
t_slam_get_tracked_pose(struct xrt_tracked_slam *xts, timepoint_ns when_ns, struct xrt_space_relation *out_relation)
{
	auto &t = *container_of(xts, TrackerSlam, base);

	//! @todo This should not be cached, the same timestamp can be requested at a
	//! later time on the frame for a better prediction.
	if (when_ns == t.last_ts) {
		*out_relation = t.last_rel;
		return;
	}

	flush_poses(t);

	predict_pose(t, when_ns, out_relation);
	t.pred_traj_writer->push(when_ns, out_relation->pose);

	filter_pose(t, when_ns, out_relation);
	t.filt_traj_writer->push(when_ns, out_relation->pose);

	t.last_rel = *out_relation;
	t.last_ts = when_ns;

	if (t.gt.override_tracking) {
		out_relation->pose = gt2xr_pose(t.gt.origin, get_gt_pose_at(*t.gt.trajectory, when_ns));
	}
}

//! Receive and register ground truth to use for trajectory error metrics.
extern "C" void
t_slam_gt_sink_push(struct xrt_pose_sink *sink, timepoint_ns ts, struct xrt_pose *pose)
{
	auto &t = *container_of(sink, TrackerSlam, gt_sink);

	if (t.gt.trajectory->empty()) {
		t.gt.origin = *pose;
		gt_ui_setup(t);
	}

	t.gt.trajectory->insert_or_assign(ts, *pose);
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
	SLAM_DASSERT(ts > t.last_imu_ts, "Sample (%ld) is older than last (%ld)", ts, t.last_imu_ts);
	t.last_imu_ts = ts;
}

//! Push the frame to the external SLAM system
static void
push_frame(TrackerSlam &t, struct xrt_frame *frame, bool is_left)
{
	SLAM_DASSERT(t.last_left_ts != INT64_MIN || is_left, "First frame was a right frame");

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
	delete t.gt.trajectory;
	delete t.slam_times_writer;
	delete t.slam_traj_writer;
	delete t.pred_traj_writer;
	delete t.filt_traj_writer;
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
	t.gt_sink.push_pose = t_slam_gt_sink_push;

	t.sinks.left = &t.left_sink;
	t.sinks.right = &t.right_sink;
	t.sinks.imu = &t.imu_sink;
	t.sinks.gt = &t.gt_sink;

	t.submit = debug_get_bool_option_slam_submit_from_start();

	t.node.break_apart = t_slam_node_break_apart;
	t.node.destroy = t_slam_node_destroy;

	int ret = os_thread_helper_init(&t.oth);
	SLAM_ASSERT(ret == 0, "Unable to initialize thread");

	xrt_frame_context_add(xfctx, &t.node);

	t.euroc_recorder = euroc_recorder_create(xfctx, NULL);

	t.pred_type = (prediction_type)debug_get_num_option_slam_prediction_type();

	m_filter_euro_vec3_init(&t.filter.pos_oe, t.filter.min_cutoff, t.filter.beta, t.filter.min_dcutoff);
	m_filter_euro_quat_init(&t.filter.rot_oe, t.filter.min_cutoff, t.filter.beta, t.filter.min_dcutoff);

	t.gt.trajectory = new Trajectory{};

	// Probe for timing extension.
	// We provide two timing columns by default, even if the external system does
	// not support the timing extension for reporting internal timestamps.
	t.timing.columns = {"sampled", "received_by_monado"};
	bool has_timing_extension = t.slam->supports_feature(F_ENABLE_POSE_EXT_TIMING);
	if (has_timing_extension) {
		shared_ptr<void> result;
		t.slam->use_feature(FID_EPET, nullptr, result);
		t.timing.ext_enabled = true;

		vector<string> cols = *std::static_pointer_cast<FRESULT_EPET>(result);
		t.timing.columns.insert(t.timing.columns.begin() + 1, cols.begin(), cols.end());
	}

	// Setup CSV files
	bool write_csvs = debug_get_bool_option_slam_write_csvs();
	t.slam_times_writer = new TimingWriter{"timing.csv", write_csvs, t.timing.columns};
	t.slam_traj_writer = new TrajectoryWriter{"tracking.csv", write_csvs};
	t.pred_traj_writer = new TrajectoryWriter{"prediction.csv", write_csvs};
	t.filt_traj_writer = new TrajectoryWriter{"filtering.csv", write_csvs};

	setup_ui(t);

	*out_xts = &t.base;
	*out_sink = &t.sinks;

	SLAM_DEBUG("SLAM tracker created");
	return 0;
}
