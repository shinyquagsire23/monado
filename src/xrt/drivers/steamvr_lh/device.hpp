// Copyright 2023, Shawn Wallace
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief SteamVR driver device header - inherits xrt_device.
 * @author Shawn Wallace <yungwallace@live.com>
 * @ingroup drv_steamvr_lh
 */

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

#include <condition_variable>
#include <mutex>

#include "xrt/xrt_device.h"
#include "openvr_driver.h"

class Context;
struct InputClass;

struct DeviceBuilder
{
	std::shared_ptr<Context> ctx;
	vr::ITrackedDeviceServerDriver *driver;
	const char *serial;
	const std::string &steam_install;

	// no copies!
	DeviceBuilder(const DeviceBuilder &) = delete;
	DeviceBuilder
	operator=(const DeviceBuilder &) = delete;
};

class Device : public xrt_device
{

public:
	xrt_space_relation relation = XRT_SPACE_RELATION_ZERO;

	virtual ~Device() = default;

	xrt_input *
	get_input_from_name(std::string_view name);

	void
	update_inputs();

	void
	update_pose(const vr::DriverPose_t &newPose);

	void
	get_tracked_pose(xrt_input_name name, uint64_t at_timestamp_ns, xrt_space_relation *out_relation);

	void
	handle_properties(const vr::PropertyWrite_t *batch, uint32_t count);

protected:
	Device(const DeviceBuilder &builder);
	std::shared_ptr<Context> ctx;
	vr::PropertyContainerHandle_t container_handle{0};

	virtual void
	handle_property_write(const vr::PropertyWrite_t &prop) = 0;

	void
	set_input_class(const InputClass *input_class);

private:
	vr::ITrackedDeviceServerDriver *driver;
	const InputClass *input_class;
	std::vector<xrt_binding_profile> binding_profiles_vec;
	std::unordered_map<std::string_view, xrt_input *> inputs_map;
	std::vector<xrt_input> inputs_vec;
	uint64_t current_frame{0};

	void
	init_chaperone(const std::string &steam_install);
	inline static xrt_vec3 chaperone_center{};
	inline static xrt_quat chaperone_yaw = XRT_QUAT_IDENTITY;
};

class HmdDevice : public Device
{
public:
	struct Parts
	{
		xrt_hmd_parts base;
		vr::IVRDisplayComponent *display;
	};

	HmdDevice(const DeviceBuilder &builder);

	void
	get_view_poses(const xrt_vec3 *default_eye_relation,
	               uint64_t at_timestamp_ns,
	               uint32_t view_count,
	               xrt_space_relation *out_head_relation,
	               xrt_fov *out_fovs,
	               xrt_pose *out_poses);

	bool
	compute_distortion(uint32_t view, float u, float v, xrt_uv_triplet *out_result);

	void
	set_hmd_parts(std::unique_ptr<Parts> parts);

private:
	std::unique_ptr<Parts> hmd_parts{nullptr};

	void
	handle_property_write(const vr::PropertyWrite_t &prop) override;

	void
	set_nominal_frame_interval(uint64_t interval_ns);

	std::condition_variable hmd_parts_cv;
	std::mutex hmd_parts_mut;
};

class ControllerDevice : public Device
{
public:
	ControllerDevice(vr::PropertyContainerHandle_t container_handle, const DeviceBuilder &builder);

	void
	set_output(xrt_output_name name, const xrt_output_value *value);

	void
	set_haptic_handle(vr::VRInputComponentHandle_t handle);

private:
	vr::VRInputComponentHandle_t haptic_handle{0};
	std::unique_ptr<xrt_output> output{nullptr};

	void
	handle_property_write(const vr::PropertyWrite_t &prop) override;
};
