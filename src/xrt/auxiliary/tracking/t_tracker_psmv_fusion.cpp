// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  PS Move tracker code that is expensive to compile.
 *
 * Typically built as a part of t_kalman.cpp to reduce incremental build times.
 *
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_tracking
 */

#include "t_tracker_psmv_fusion.h"

#include "tracking/t_fusion.h"
#include "tracking/t_imu_fusion.h"

#include "math/m_api.h"
#include "math/m_eigen_interop.h"

#include "util/u_misc.h"

#include "flexkalman/AbsoluteOrientationMeasurement.h"
#include "flexkalman/FlexibleKalmanFilter.h"
#include "flexkalman/FlexibleUnscentedCorrect.h"
#include "flexkalman/PoseSeparatelyDampedConstantVelocity.h"
#include "flexkalman/PoseState.h"


using State = flexkalman::pose_externalized_rotation::State;
using ProcessModel =
    flexkalman::PoseSeparatelyDampedConstantVelocityProcessModel;

namespace xrt_fusion {

struct TrackingInfo
{
	bool valid{false};
	bool tracked{false};
};
namespace {
	class PSMVFusion : public PSMVFusionInterface
	{
	public:
		EIGEN_MAKE_ALIGNED_OPERATOR_NEW

		void
		clear_position_tracked_flag() override;

		void
		process_imu_data(time_duration_ns delta_ns,
		                 const struct xrt_tracking_sample *sample,
		                 const struct xrt_vec3
		                     *orientation_variance_optional) override;
		void
		process_3d_vision_data(
		    time_duration_ns delta_ns,
		    const struct xrt_vec3 *position,
		    const struct xrt_vec3 *variance_optional,
		    const struct xrt_vec3 *lever_arm_optional,
		    float residual_limit) override;

		void
		get_prediction(
		    timepoint_ns when_ns,
		    struct xrt_space_relation *out_relation) override;

	private:
		void
		reset_filter();
		void
		reset_filter_and_imu();

		State filter_state;
		ProcessModel process_model;

		xrt_fusion::SimpleIMUFusion imu;

		bool tracked{false};
		TrackingInfo orientation_state;
		TrackingInfo position_state;
	};



	void
	PSMVFusion::clear_position_tracked_flag()
	{
		position_state.tracked = false;
	}

	void
	PSMVFusion::reset_filter()
	{
		filter_state = State{};
		tracked = false;
		position_state = TrackingInfo{};
	}
	void
	PSMVFusion::reset_filter_and_imu()
	{
		reset_filter();
		orientation_state = TrackingInfo{};
		imu = SimpleIMUFusion{};
	}

	void
	PSMVFusion::process_imu_data(
	    time_duration_ns delta_ns,
	    const struct xrt_tracking_sample *sample,
	    const struct xrt_vec3 *orientation_variance_optional)
	{

		float dt = time_ns_to_s(delta_ns);
		Eigen::Vector3d variance = Eigen::Vector3d::Constant(0.01);
		if (orientation_variance_optional) {
			variance = map_vec3(*orientation_variance_optional)
			               .cast<double>();
		}
		imu.handleAccel(map_vec3(sample->accel_m_s2).cast<double>(),
		                dt);
		imu.handleGyro(map_vec3(sample->gyro_rad_secs).cast<double>(),
		               dt);
		imu.postCorrect();

		//! @todo use better measurements instead of the above "simple
		//! fusion"
		flexkalman::predict(filter_state, process_model, dt);
		auto meas = flexkalman::AbsoluteOrientationMeasurement{
		    // Must rotate by 180 to align
		    Eigen::Quaterniond(
		        Eigen::AngleAxisd(EIGEN_PI, Eigen::Vector3d::UnitY())) *
		        imu.getQuat(),
		    variance};
		if (flexkalman::correctUnscented(filter_state, meas)) {
			orientation_state.tracked = true;
			orientation_state.valid = true;
		} else {
			fprintf(stderr,
			        "Got non-finite something when filtering IMU - "
			        "resetting filter and IMU fusion!\n");
			reset_filter_and_imu();
		}
	}

	void
	PSMVFusion::process_3d_vision_data(
	    time_duration_ns delta_ns,
	    const struct xrt_vec3 *position,
	    const struct xrt_vec3 *variance_optional,
	    const struct xrt_vec3 *lever_arm_optional,
	    float residual_limit)
	{
		Eigen::Vector3f pos = map_vec3(*position);
		Eigen::Vector3d variance{1.e-4, 1.e-4, 4.e-4};
		if (variance_optional) {
			variance = map_vec3(*variance_optional).cast<double>();
		}
		Eigen::Vector3d lever_arm{0, 0.09, 0};
		if (lever_arm_optional) {
			lever_arm =
			    map_vec3(*lever_arm_optional).cast<double>();
		}
		auto measurement =
		    xrt_fusion::AbsolutePositionLeverArmMeasurement{
		        pos.cast<double>(), lever_arm, variance};
		double resid = measurement.getResidual(filter_state).norm();

		if (resid > residual_limit) {
			// Residual arbitrarily "too large"
			fprintf(
			    stderr,
			    "Warning - measurement residual is %f, resetting "
			    "filter state\n",
			    resid);
			reset_filter();
			return;
		}
		if (flexkalman::correctUnscented(filter_state, measurement)) {
			tracked = true;
			position_state.valid = true;
			position_state.tracked = true;
		} else {
			fprintf(stderr,
			        "Got non-finite something when filtering "
			        "tracker - resetting filter!\n");
			reset_filter();
		}
	}

	void
	PSMVFusion::get_prediction(timepoint_ns when_ns,
	                           struct xrt_space_relation *out_relation)
	{
		if (out_relation == NULL) {
			return;
		}
		// Clear to sane values
		U_ZERO(out_relation);
		out_relation->pose.orientation.w = 1;
		if (!tracked) {
			return;
		}
		auto predicted_state = flexkalman::getPrediction(
		    filter_state, process_model,
		    /*! @todo compute dt here */ 0.024);

		map_vec3(out_relation->pose.position) =
		    predicted_state.position().cast<float>();
		map_quat(out_relation->pose.orientation) =
		    predicted_state.getQuaternion().cast<float>();
		map_vec3(out_relation->linear_velocity) =
		    predicted_state.velocity().cast<float>();
		map_vec3(out_relation->angular_velocity) =
		    predicted_state.angularVelocity().cast<float>();

		uint64_t flags = 0;
		if (position_state.valid) {
			flags |= XRT_SPACE_RELATION_POSITION_VALID_BIT;
			flags |= XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT;
			if (position_state.tracked) {
				flags |=
				    XRT_SPACE_RELATION_POSITION_TRACKED_BIT;
			}
		}
		if (orientation_state.valid) {
			flags |= XRT_SPACE_RELATION_ORIENTATION_VALID_BIT;
			flags |= XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT;
			if (orientation_state.tracked) {
				flags |=
				    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT;
			}
		}
		out_relation->relation_flags = (xrt_space_relation_flags)flags;
	}
} // namespace


std::unique_ptr<PSMVFusionInterface>
PSMVFusionInterface::create()
{
	auto ret = std::make_unique<PSMVFusion>();
	return ret;
}
} // namespace xrt_fusion