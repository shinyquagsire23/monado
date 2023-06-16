// Copyright 2023, Shawn Wallace
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief SteamVR driver device implementation.
 * @author Shawn Wallace <yungwallace@live.com>
 * @ingroup drv_steamvr_lh
 */

#include <functional>
#include <cstring>
#include <thread>
#include <algorithm>

#include "math/m_api.h"
#include "device.hpp"
#include "interfaces/context.hpp"
#include "util/u_device.h"
#include "util/u_logging.h"
#include "util/u_json.hpp"
#include "xrt/xrt_device.h"

#define DEV_ERR(...) U_LOG_IFL_E(ctx->log_level, __VA_ARGS__)
#define DEV_WARN(...) U_LOG_IFL_W(ctx->log_level, __VA_ARGS__)
#define DEV_INFO(...) U_LOG_IFL_I(ctx->log_level, __VA_ARGS__)
#define DEV_DEBUG(...) U_LOG_IFL_D(ctx->log_level, __VA_ARGS__)

// Each device will have its own input class.
struct InputClass
{
	xrt_device_name name;
	std::string description;
	const std::vector<xrt_input_name> poses;
	const std::unordered_map<std::string_view, xrt_input_name> non_poses;
};

namespace {
InputClass hmd_class{{}, "Generic HMD", {XRT_INPUT_GENERIC_HEAD_POSE}, {}};

// Adding support for a new controller is a simple as adding it here.
// The key for the map needs to be the name of input profile as indicated by the lighthouse driver.
const std::unordered_map<std::string_view, InputClass> controller_classes{
    {
        "vive_controller",
        InputClass{
            XRT_DEVICE_VIVE_WAND,
            "Vive Wand",
            {
                XRT_INPUT_VIVE_GRIP_POSE,
                XRT_INPUT_VIVE_AIM_POSE,
            },
            {
                {"/input/application_menu/click", XRT_INPUT_VIVE_MENU_CLICK},
                {"/input/trackpad/click", XRT_INPUT_VIVE_TRACKPAD_CLICK},
                {"/input/trackpad/touch", XRT_INPUT_VIVE_TRACKPAD_TOUCH},
                {"/input/system/click", XRT_INPUT_VIVE_SYSTEM_CLICK},
                {"/input/trigger/click", XRT_INPUT_VIVE_TRIGGER_CLICK},
                {"/input/trigger/value", XRT_INPUT_VIVE_TRIGGER_VALUE},
                {"/input/grip/click", XRT_INPUT_VIVE_SQUEEZE_CLICK},
                {"/input/trackpad", XRT_INPUT_VIVE_TRACKPAD},
            },
        },
    },
};

// Template for calling a member function of Device from a free function
template <typename DeviceType, auto Func, typename Ret, typename... Args>
inline Ret
device_bouncer(struct xrt_device *xdev, Args... args)
{
	auto *dev = static_cast<DeviceType *>(xdev);
	return std::invoke(Func, dev, args...);
}
} // namespace

HmdDevice::HmdDevice(const DeviceBuilder &builder) : Device(builder)
{
	this->name = XRT_DEVICE_GENERIC_HMD;
	this->device_type = XRT_DEVICE_TYPE_HMD;
	this->container_handle = 0;

	set_input_class(&hmd_class);

#define SETUP_MEMBER_FUNC(name) this->xrt_device::name = &device_bouncer<HmdDevice, &HmdDevice::name>
	SETUP_MEMBER_FUNC(get_view_poses);
	SETUP_MEMBER_FUNC(compute_distortion);
#undef SETUP_MEMBER_FUNC
}

ControllerDevice::ControllerDevice(vr::PropertyContainerHandle_t handle, const DeviceBuilder &builder) : Device(builder)
{
	this->device_type = XRT_DEVICE_TYPE_ANY_HAND_CONTROLLER;
	this->container_handle = handle;
	this->xrt_device::set_output = &device_bouncer<ControllerDevice, &ControllerDevice::set_output>;
}

Device::Device(const DeviceBuilder &builder) : xrt_device({}), ctx(builder.ctx), driver(builder.driver)
{
	std::strncpy(this->serial, builder.serial, XRT_DEVICE_NAME_LEN - 1);
	this->serial[XRT_DEVICE_NAME_LEN - 1] = 0;
	this->tracking_origin = ctx.get();
	this->orientation_tracking_supported = true;
	this->position_tracking_supported = true;
	this->hand_tracking_supported = false;
	this->force_feedback_supported = false;
	this->form_factor_check_supported = false;

#define SETUP_MEMBER_FUNC(name) this->xrt_device::name = &device_bouncer<Device, &Device::name>
	SETUP_MEMBER_FUNC(update_inputs);
	SETUP_MEMBER_FUNC(get_tracked_pose);
#undef SETUP_MEMBER_FUNC

	this->xrt_device::destroy = [](xrt_device *xdev) {
		auto *dev = static_cast<Device *>(xdev);
		dev->driver->Deactivate();
		delete dev;
	};

	init_chaperone(builder.steam_install);
}

void
Device::set_input_class(const InputClass *input_class)
{
	// this should only be called once
	assert(inputs_vec.empty());
	this->input_class = input_class;

	// reserve to ensure our pointers don't get invalidated
	inputs_vec.reserve(input_class->poses.size() + input_class->non_poses.size());
	for (xrt_input_name input : input_class->poses) {
		inputs_vec.push_back({true, 0, input, {}});
	}
	for (const auto &[path, input] : input_class->non_poses) {
		assert(inputs_vec.capacity() >= inputs_vec.size() + 1);
		inputs_vec.push_back({true, 0, input, {}});
		inputs_map.insert({path, &inputs_vec.back()});
	}

	this->inputs = inputs_vec.data();
	this->input_count = inputs_vec.size();
}

xrt_input *
Device::get_input_from_name(const std::string_view name)
{
	auto input = inputs_map.find(name);
	if (input == inputs_map.end()) {
		DEV_WARN("requested unknown input name %s for device %s", std::string(name).c_str(), serial);
		return nullptr;
	}
	return input->second;
}

void
ControllerDevice::set_haptic_handle(vr::VRInputComponentHandle_t handle)
{
	// this should only be set once
	assert(output == nullptr);
	DEV_DEBUG("setting haptic handle for %lu", handle);
	haptic_handle = handle;
	xrt_output_name name;
	switch (this->name) {
	case XRT_DEVICE_VIVE_WAND: {
		name = XRT_OUTPUT_NAME_VIVE_HAPTIC;
		break;
	}
	default: {
		DEV_WARN("Unknown device name (%u), haptics will not work", this->name);
		return;
	}
	}
	output = std::make_unique<xrt_output>(xrt_output{name});
	this->output_count = 1;
	this->outputs = output.get();
}

void
Device::update_inputs()
{
	ctx->maybe_run_frame(++current_frame);
}

void
Device::get_tracked_pose(xrt_input_name name, uint64_t at_timestamp_ns, xrt_space_relation *out_relation)
{
	*out_relation = this->relation;
}

void
ControllerDevice::set_output(xrt_output_name name, const xrt_output_value *value)

{
	const auto &vib = value->vibration;
	if (vib.amplitude == 0.0)
		return;
	vr::VREvent_HapticVibration_t event;
	event.containerHandle = container_handle;
	event.componentHandle = haptic_handle;
	event.fDurationSeconds = (float)vib.duration_ns / 1e9f;
	// 0.0f in OpenXR means let the driver determine a frequency, but
	// in OpenVR means no haptic.
	event.fFrequency = std::max(vib.frequency, 1.0f);
	event.fAmplitude = vib.amplitude;

	ctx->add_haptic_event(event);
}

void
HmdDevice::get_view_poses(const xrt_vec3 *default_eye_relation,
                          uint64_t at_timestamp_ns,
                          uint32_t view_count,
                          xrt_space_relation *out_head_relation,
                          xrt_fov *out_fovs,
                          xrt_pose *out_poses)
{
	u_device_get_view_poses(this, default_eye_relation, at_timestamp_ns, view_count, out_head_relation, out_fovs,
	                        out_poses);
}

bool
HmdDevice::compute_distortion(uint32_t view, float u, float v, xrt_uv_triplet *out_result)
{
	vr::EVREye eye = (view == 0) ? vr::Eye_Left : vr::Eye_Right;
	vr::DistortionCoordinates_t coords = this->hmd_parts->display->ComputeDistortion(eye, u, v);
	out_result->r = {coords.rfRed[0], coords.rfRed[1]};
	out_result->g = {coords.rfGreen[0], coords.rfGreen[1]};
	out_result->b = {coords.rfBlue[0], coords.rfBlue[1]};
	return true;
}

void
HmdDevice::set_hmd_parts(std::unique_ptr<Parts> parts)
{
	{
		std::lock_guard lk(hmd_parts_mut);
		hmd_parts = std::move(parts);
		this->hmd = &hmd_parts->base;
	}
	hmd_parts_cv.notify_all();
}

namespace {
xrt_quat
copy_quat(const vr::HmdQuaternion_t &quat)
{
	return xrt_quat{(float)quat.x, (float)quat.y, (float)quat.z, (float)quat.w};
}

xrt_vec3
copy_vec3(const double (&vec)[3])
{
	return xrt_vec3{(float)vec[0], (float)vec[1], (float)vec[2]};
}
} // namespace

void
Device::init_chaperone(const std::string &steam_install)
{
	static bool initialized = false;
	if (initialized)
		return;

	initialized = true;

	// Lighthouse driver seems to create a lighthousedb.json and a chaperone_info.vrchap (which is json)
	// We will use the known_universes from the lighthousedb.json to match to a universe from chaperone_info.vrchap

	using xrt::auxiliary::util::json::JSONNode;
	auto lighthousedb = JSONNode::loadFromFile(steam_install + "/config/lighthouse/lighthousedb.json");
	if (lighthousedb.isInvalid()) {
		DEV_ERR("Couldn't load lighthousedb file, playspace center will be off - was Room Setup run?");
		return;
	}
	auto chap_info = JSONNode::loadFromFile(steam_install + "/config/chaperone_info.vrchap");
	if (chap_info.isInvalid()) {
		DEV_ERR("Couldn't load chaperone info, playspace center will be off - was Room Setup run?");
		return;
	}

	// XXX: This may be broken if there are multiple known universes - how do we determine which to use then?
	JSONNode universe = lighthousedb["known_universes"][0];
	std::string id = universe["id"].asString();
	JSONNode info;
	for (const JSONNode &u : chap_info["universes"].asArray()) {
		if (u["universeID"].asString() == id) {
			DEV_INFO("Found info for universe %s", id.c_str());
			info = u;
			break;
		}
	}

	if (info.isInvalid()) {
		DEV_ERR("Couldn't find chaperone info for universe %s, playspace center will be off", id.c_str());
		return;
	}

	std::vector<JSONNode> translation_arr = info["standing"]["translation"].asArray();
	double yaw = info["standing"]["yaw"].asDouble();
	chaperone_center = {static_cast<float>(translation_arr[0].asDouble()),
	                    static_cast<float>(translation_arr[1].asDouble()),
	                    static_cast<float>(translation_arr[2].asDouble())};
	const xrt_vec3 yaw_axis{0.0, -1.0, 0.0};
	math_quat_from_angle_vector(yaw, &yaw_axis, &chaperone_yaw);
	DEV_INFO("Initialized chaperone data.");
}

void
Device::update_pose(const vr::DriverPose_t &newPose)
{
	xrt_space_relation relation;
	if (newPose.poseIsValid) {
		relation.relation_flags = XRT_SPACE_RELATION_BITMASK_ALL;

		const xrt_vec3 to_local_pos = copy_vec3(newPose.vecDriverFromHeadTranslation);
		const xrt_quat to_local_rot = copy_quat(newPose.qDriverFromHeadRotation);
		const xrt_vec3 to_world_pos = copy_vec3(newPose.vecWorldFromDriverTranslation);
		const xrt_quat to_world_rot = copy_quat(newPose.qWorldFromDriverRotation);

		xrt_pose &pose = relation.pose;
		pose.position = copy_vec3(newPose.vecPosition);
		pose.orientation = copy_quat(newPose.qRotation);
		relation.linear_velocity = copy_vec3(newPose.vecVelocity);
		relation.angular_velocity = copy_vec3(newPose.vecAngularVelocity);

		// apply world transform
		auto world_transform = [&](xrt_vec3 &vec) {
			math_quat_rotate_vec3(&to_world_rot, &vec, &vec);
			math_vec3_accum(&to_world_pos, &vec);
		};
		world_transform(pose.position);
		world_transform(relation.linear_velocity);
		math_quat_rotate(&to_world_rot, &pose.orientation, &pose.orientation);
		math_quat_rotate_vec3(&pose.orientation, &relation.angular_velocity, &relation.angular_velocity);

		// apply local transform
		xrt_vec3 local_rotated;
		math_quat_rotate_vec3(&pose.orientation, &to_local_pos, &local_rotated);
		math_vec3_accum(&local_rotated, &pose.position);
		math_vec3_accum(&local_rotated, &relation.linear_velocity);
		math_quat_rotate(&pose.orientation, &to_local_rot, &pose.orientation);

		// apply chaperone transform
		auto chap_transform = [&](xrt_vec3 &vec) {
			math_vec3_accum(&chaperone_center, &vec);
			math_quat_rotate_vec3(&chaperone_yaw, &vec, &vec);
		};
		chap_transform(pose.position);
		chap_transform(relation.linear_velocity);
		math_quat_rotate(&chaperone_yaw, &pose.orientation, &pose.orientation);
	} else {
		relation.relation_flags = XRT_SPACE_RELATION_BITMASK_NONE;
	}
	this->relation = relation;
}

void
Device::handle_properties(const vr::PropertyWrite_t *batch, uint32_t count)
{
	for (uint32_t i = 0; i < count; ++i) {
		handle_property_write(batch[i]);
	}
}

void
HmdDevice::set_nominal_frame_interval(uint64_t interval_ns)
{
	auto set = [this, interval_ns] { hmd_parts->base.screens[0].nominal_frame_interval_ns = interval_ns; };

	if (hmd_parts) {
		set();
	} else {
		std::thread t([this, set] {
			std::unique_lock lk(hmd_parts_mut);
			hmd_parts_cv.wait(lk, [this] { return hmd_parts != nullptr; });
			set();
		});
		t.detach();
	}
}

namespace {
// From openvr driver documentation
// (https://github.com/ValveSoftware/openvr/blob/master/docs/Driver_API_Documentation.md#Input-Profiles):
// "Input profiles are expected to be a valid JSON file,
// and should be located: <driver_name>/resources/input/<device_name>_profile.json"
// So we will just parse the file name to get the device name.
std::string_view
parse_profile(std::string_view path)
{
	size_t name_start_idx = path.find_last_of('/') + 1;
	size_t name_end_idx = path.find_last_of('_');
	return path.substr(name_start_idx, name_end_idx - name_start_idx);
}
} // namespace

void
HmdDevice::handle_property_write(const vr::PropertyWrite_t &prop)
{
	switch (prop.prop) {
	case vr::Prop_DisplayFrequency_Float: {
		assert(prop.unBufferSize == sizeof(float));
		float freq = *static_cast<float *>(prop.pvBuffer);
		set_nominal_frame_interval((1.f / freq) * 1e9f);
		break;
	}
	case vr::Prop_InputProfilePath_String: {
		std::string_view profile =
		    parse_profile(std::string_view(static_cast<char *>(prop.pvBuffer), prop.unBufferSize));
		if (profile == "vive") {
			std::strcpy(this->str, "Vive HMD");
		}
	}
	default: break;
	}
}

void
ControllerDevice::handle_property_write(const vr::PropertyWrite_t &prop)
{
	switch (prop.prop) {
	case vr::Prop_InputProfilePath_String: {
		std::string_view profile =
		    parse_profile(std::string_view(static_cast<char *>(prop.pvBuffer), prop.unBufferSize));
		auto input_class = controller_classes.find(profile);
		if (input_class == controller_classes.end()) {
			DEV_ERR("Could not find input class for controller profile %s", std::string(profile).c_str());
		} else {
			std::strcpy(this->str, input_class->second.description.c_str());
			this->name = input_class->second.name;
			set_input_class(&input_class->second);
		}
		break;
	}
	default: break;
	}
}
