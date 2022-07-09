// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  SLAM tracker class header for usage in Monado.
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup aux_tracking
 *
 * This file contains the declaration of the @ref slam_tracker class. This
 * header is intended to appear in both Monado and an external SLAM system. The
 * implementation of `slam_tracker` is provided by the external system.
 * Additional data types are declared for the communication between Monado and
 * the system.
 *
 */

#pragma once

#include <opencv2/core/mat.hpp>

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <chrono>

namespace xrt::auxiliary::tracking::slam {

// For implementation: same as IMPLEMENTATION_VERSION_*
// For user: expected IMPLEMENTATION_VERSION_*. Should be checked in runtime.
constexpr int HEADER_VERSION_MAJOR = 4; //!< API Breakages
constexpr int HEADER_VERSION_MINOR = 0; //!< Backwards compatible API changes
constexpr int HEADER_VERSION_PATCH = 0; //!< Backw. comp. .h-implemented changes

// Which header version the external system is implementing.
extern const int IMPLEMENTATION_VERSION_MAJOR;
extern const int IMPLEMENTATION_VERSION_MINOR;
extern const int IMPLEMENTATION_VERSION_PATCH;

enum class pose_ext_type : int;

/*!
 * @brief Standard pose type to communicate Monado with the external SLAM system
 */
struct pose {
  std::int64_t timestamp;   //!< In same clock as input samples
  float px, py, pz;         //!< Position vector
  float rx, ry, rz, rw = 1; //!< Orientation quaternion
  std::shared_ptr<struct pose_extension> next = nullptr;

  pose() = default;
  pose(std::int64_t timestamp,       //
       float px, float py, float pz, //
       float rx, float ry, float rz, float rw)
      : timestamp(timestamp),   //
        px(px), py(py), pz(pz), //
        rx(rx), ry(ry), rz(rz), rw(rw) {}

  std::shared_ptr<pose_extension>
  find_pose_extension(pose_ext_type required_type) const;
};

/*!
 * @brief IMU Sample type to pass around between programs
 */
struct imu_sample {
  std::int64_t timestamp; //!< In nanoseconds
  double ax, ay, az;      //!< Accel in meters per second squared (m / s^2)
  double wx, wy, wz;      //!< Gyro in radians per second (rad / s)
  imu_sample() = default;
  imu_sample(std::int64_t timestamp, double ax, double ay, double az, double wx,
             double wy, double wz)
      : timestamp(timestamp), ax(ax), ay(ay), az(az), wx(wx), wy(wy), wz(wz) {}
};

/*!
 * @brief Image sample type to pass around between programs. It is expected that
 * any SLAM system takes OpenCV matrices as input.
 */
struct img_sample {
  std::int64_t timestamp;
  cv::Mat img;
  bool is_left;
  img_sample() = default;
  img_sample(std::int64_t timestamp, const cv::Mat &img, bool is_left)
      : timestamp(timestamp), img(img), is_left(is_left) {}
};

/*!
 * @brief slam_tracker serves as an interface between Monado and external SLAM
 * systems.
 *
 * This class uses the pointer-to-implementation pattern, and its implementation
 * should be provided by an external SLAM system.
 */
struct slam_tracker {
  /*!
   * @brief Construct a new slam tracker object
   *
   * @param config_file SLAM systems parameters tend to be numerous and very
   * specific, so they usually use a configuration file as the main way to set
   * them up. Therefore, this constructor receives a path to a
   * implementation-specific configuration file.
   */
  slam_tracker(const std::string &config_file);
  ~slam_tracker();

  slam_tracker(const slam_tracker &) = delete;
  slam_tracker &operator=(const slam_tracker &) = delete;

  void initialize();
  void start();
  bool is_running();
  void stop();
  void finalize();

  /*!
   * @brief Push an IMU sample into the tracker.
   *
   * There must be a single producer thread pushing samples.
   * Samples must have monotonically increasing timestamps.
   * The implementation must be non-blocking.
   * Thus, a separate consumer thread should process the samples.
   */
  void push_imu_sample(const imu_sample &sample);

  /*!
   * @brief Push an image sample into the tracker.
   *
   * Same conditions as @ref push_imu_sample apply.
   * When using stereo frames, they must be pushed in a left-right order.
   * The consecutive left-right pair must have the same timestamps.
   */
  void push_frame(const img_sample &sample);

  /*!
   * @brief Get the latest tracked pose from the SLAM system.
   *
   * There must be a single thread consuming this method.
   *
   * @param[out] out_pose Dequeued pose.
   * @return true If a new pose was dequeued into @p out_pose.
   * @return false If there was no pose to dequeue.
   */
  bool try_dequeue_pose(pose &out_pose);

  //! Asks the SLAM system whether it supports a specific feature.
  bool supports_feature(int feature_id);

  /*!
   * @brief Use a special feature of the SLAM tracker.
   *
   * This method uses heap allocated objects for passing parameters and
   * obtaining the results. Use `std::static_pointer_cast` to shared pointers to
   * the expected types.
   *
   * @param feature_id Id of the special feature.
   * @param params Pointer to the parameter object for this feature.
   * @param result Pointer to the result produced by the feature call.
   * @return false if the feature was not supported, true otherwise.
   */
  bool use_feature(int feature_id, const std::shared_ptr<void> &params,
                   std::shared_ptr<void> &result);

private:
  struct implementation;
  std::unique_ptr<implementation> impl;
};

/*
 * Special features
 *
 * A special feature is comprised of an ID, a PARAMS type and a RESULT type. It
 * can be defined using DEFINE_FEATURE. Once defined, the definition should not
 * suffer future changes.
 *
 * One of the main concerns in the features interface is the ability to add new
 * features without being required to update the SLAM systems that are not
 * interested in implementing the feature.
 *
 */

#define DEFINE_FEATURE(NAME, SHORT_NAME, ID, PARAMS_TYPE, RESULT_TYPE)         \
  using FPARAMS_##SHORT_NAME = PARAMS_TYPE;                                    \
  using FRESULT_##SHORT_NAME = RESULT_TYPE;                                    \
  constexpr int FID_##SHORT_NAME = ID;                                         \
  constexpr int F_##NAME = ID;

/*!
 * Container of parameters for a pinhole camera calibration (fx, fy, cx, cy)
 * with an optional distortion.
 *
 *`distortion_model` and its corresponding `distortion` parameters are not
 * standardized in this struct to facilitate implementation prototyping.
 */
struct cam_calibration {
  int cam_index; //!< For multi-camera setups. For stereo 0 ~ left, 1 ~ right.
  int width, height;                //<! Resolution
  double frequency;                 //<! Frames per second
  double fx, fy;                    //<! Focal point
  double cx, cy;                    //<! Principal point
  std::string distortion_model;     //!< Models like: none, rt4, rt5, rt8, kb4
  std::vector<double> distortion{}; //!< Parameters for the distortion_model
  cv::Matx<double, 4, 4> t_imu_cam; //!< Transformation from IMU to camera
};

struct inertial_calibration {
  // Calibration intrinsics to apply to each raw measurement.

  //! This transform will be applied to raw measurements.
  cv::Matx<double, 3, 3> transform;

  //! Offset to add to raw measurements to; called bias in other contexts.
  cv::Matx<double, 3, 1> offset;

  // Parameters for the random processes that model this IMU. See section "2.1
  // Gyro Noise Model" of N. Trawny and S. I. Roumeliotis, "Indirect Kalman
  // Filter for 3D Attitude Estimation". Analogous for accelerometers.
  // http://mars.cs.umn.edu/tr/reports/Trawny05b.pdf#page=15

  //! IMU internal bias ~ wiener process with steps N(0, σ²); this field is σ;
  //! [σ] = U / sqrt(sec³) with U = rad if gyroscope, U = m/s if accelerometer.
  cv::Matx<double, 3, 1> bias_std;

  //! IMU measurement noise ~ N(0, σ²); this field is σ.
  //! [σ] = U / sqrt(sec) with U = rad if gyroscope, U = m/s if accelerometer.
  cv::Matx<double, 3, 1> noise_std;

  inertial_calibration() : transform(cv::Matx<double, 3, 3>::eye()) {}
};

struct imu_calibration {
  int imu_index;    //!< For multi-imu setups. Usually just 0.
  double frequency; //!< Samples per second
  inertial_calibration accel;
  inertial_calibration gyro;
};

/*!
 * Feature ADD_CAMERA_CALIBRATION
 *
 * Use it after constructor but before `start()` to write or overwrite camera
 * calibration data that might come from the system-specific config file.
 */
DEFINE_FEATURE(ADD_CAMERA_CALIBRATION, ACC, 1, cam_calibration, void)

/*!
 * Feature ADD_IMU_CALIBRATION
 *
 * Use it after constructor but before `start()` to write or overwrite IMU
 * calibration data that might come from the system-specific config file.
 */
DEFINE_FEATURE(ADD_IMU_CALIBRATION, AIC, 2, imu_calibration, void)

/*!
 * Feature ENABLE_POSE_EXT_TIMING
 *
 * Enable/disable adding internal timestamps to the estimated poses.
 * Returns a vector with names for the timestamps in `pose_ext_timing`.
 */
DEFINE_FEATURE(ENABLE_POSE_EXT_TIMING, EPET, 3, bool, std::vector<std::string>)

/*!
 * Feature ENABLE_POSE_EXT_FEATURES
 *
 * Enable/disable adding feature information to the estimated poses.
 */
DEFINE_FEATURE(ENABLE_POSE_EXT_FEATURES, EPEF, 4, bool, void)

/*
 * Pose extensions
 *
 * A pose extension is a struct that gets linked in the `pose.next` field. You
 * first ask if the implementation supports enabling such extension with a
 * `supports_feature()` call with the appropriate `ENABLE_POSE_EXT_*`. Then, it
 * can be enabled with the corresponding `use_feature()` call.
 *
 */

enum class pose_ext_type : int {
  UNDEFINED = 0,
  TIMING = 1,
  FEATURES = 2,
};

struct pose_extension {
  pose_ext_type type = pose_ext_type::UNDEFINED;
  std::shared_ptr<pose_extension> next = nullptr;

  pose_extension(pose_ext_type type) : type(type) {}
};

inline std::shared_ptr<pose_extension>
pose::find_pose_extension(pose_ext_type required_type) const {
  std::shared_ptr<pose_extension> pe = next;
  while (pe != nullptr && pe->type != required_type) {
    pe = pe->next;
  }
  return pe;
}

// Timing pose extension
struct pose_ext_timing_data {
  //! Internal pipeline stage timestamps of interest when generating the pose.
  //! In steady clock ns. Must have the same number of elements in the same run.
  std::vector<std::int64_t> timing{};

  //! Names of each timing stage. Should point to static memory.
  const std::vector<std::string> *timing_titles = nullptr;
};

struct pose_ext_timing : pose_extension, pose_ext_timing_data {
  pose_ext_timing() : pose_extension{pose_ext_type::TIMING} {}
  pose_ext_timing(const pose_ext_timing_data &petd)
      : pose_extension{pose_ext_type::TIMING}, pose_ext_timing_data{petd} {}
};

// Features pose extension
struct pose_ext_features_data {
  struct feature {
    std::int64_t id;
    float u;
    float v;
    float depth;
  };

  std::vector<std::vector<feature>> features_per_cam{};
};

struct pose_ext_features : pose_extension, pose_ext_features_data {
  pose_ext_features() : pose_extension{pose_ext_type::FEATURES} {}
  pose_ext_features(const pose_ext_features_data &pefd)
      : pose_extension{pose_ext_type::FEATURES}, pose_ext_features_data{pefd} {}
};

/*!
 * Utility object to keep track of different stats for a particular timestamp.
 * Stats usually correspond with a particular pose extension.
 */
struct timestats : pose_ext_timing_data, pose_ext_features_data {
  using ptr = std::shared_ptr<timestats>;

  std::int64_t ts = -1;
  bool timing_enabled = false;
  bool features_enabled = false;

  void addTime(const char *name, int64_t ts = INT64_MIN) {
    if (!timing_enabled) {
      return;
    }
    if (timing_titles) {
      std::string expected = timing_titles->at(timing.size());
      if (expected != name) {
        std::cout << "Invalid timing stage\n";
        std::cout << "expected: " << expected;
        std::cout << ", got: " << name << std::endl;
        exit(EXIT_FAILURE);
      }
    }
    if (ts == INT64_MIN) {
      ts = std::chrono::steady_clock::now().time_since_epoch().count();
    }
    timing.push_back(ts);
  }

  void addFeature(size_t cam, const feature &f) {
    if (!features_enabled) {
      return;
    }
    if (cam >= features_per_cam.size()) {
      features_per_cam.resize(cam + 1);
    }
    features_per_cam.at(cam).push_back(f);
  }
};

} // namespace xrt::auxiliary::tracking::slam
