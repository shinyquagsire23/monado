// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Main driver code for @ref st_ovrd.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @ingroup st_ovrd
 */

#include <cstring>
#include <thread>
#include <sstream>

#include "ovrd_log.hpp"
#include "openvr_driver.h"

extern "C" {
#include "ovrd_interface.h"

#include <math.h>

#include <math/m_space.h>
#include "os/os_time.h"
#include "util/u_debug.h"
#include "util/u_device.h"

#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_instance.h"

#include "bindings/b_generated_bindings.h"
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

//! When set, all controllers pretend to be Index controllers. Provides best
//! compatibility with legacy games due to steamvr's legacy binding for Index
//! controllers, but input mapping may be incomplete or not ideal.
DEBUG_GET_ONCE_BOOL_OPTION(emulate_index_controller, "STEAMVR_EMULATE_INDEX_CONTROLLER", false)

DEBUG_GET_ONCE_NUM_OPTION(scale_percentage, "XRT_COMPOSITOR_SCALE_PERCENTAGE", 140)

//#define DUMP_POSE
//#define DUMP_POSE_CONTROLLERS


/*
 * Controller
 */

struct MonadoInputComponent
{
	bool has_component;
	bool x;
	bool y;
};

struct SteamVRDriverControl
{
	const char *steamvr_control_path;
	vr::VRInputComponentHandle_t control_handle;
};

struct SteamVRDriverControlInput : SteamVRDriverControl
{
	enum xrt_input_type monado_input_type;
	enum xrt_input_name monado_input_name;

	struct MonadoInputComponent component;
};

struct SteamVRDriverControlOutput : SteamVRDriverControl
{
	enum xrt_output_type monado_output_type;
	enum xrt_output_name monado_output_name;
};

static void
copy_vec3(struct xrt_vec3 *from, double *to)
{
	to[0] = from->x;
	to[1] = from->y;
	to[2] = from->z;
}

static void
copy_quat(struct xrt_quat *from, vr::HmdQuaternion_t *to)
{
	to->x = from->x;
	to->y = from->y;
	to->z = from->z;
	to->w = from->w;
}

static void
apply_pose(struct xrt_space_relation *rel, vr::DriverPose_t *m_pose)
{
	if ((rel->relation_flags & XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT) != 0) {
		copy_quat(&rel->pose.orientation, &m_pose->qRotation);
	} else {
		m_pose->result = vr::TrackingResult_Running_OutOfRange;
		m_pose->poseIsValid = false;
	}

	if ((rel->relation_flags & XRT_SPACE_RELATION_POSITION_TRACKED_BIT) != 0) {
		copy_vec3(&rel->pose.position, m_pose->vecPosition);
	} else {
	}

	if ((rel->relation_flags & XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT) != 0) {
		// linear velocity in world space
		copy_vec3(&rel->linear_velocity, m_pose->vecVelocity);
	}

	if ((rel->relation_flags & XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT) != 0) {
		// angular velocity reported by monado in world space,
		// expected by steamvr to be in "controller space"
		struct xrt_quat orientation_inv;
		math_quat_invert(&rel->pose.orientation, &orientation_inv);

		struct xrt_vec3 vel;
		math_quat_rotate_derivative(&orientation_inv, &rel->angular_velocity, &vel);

		copy_vec3(&vel, m_pose->vecAngularVelocity);
	}
}

class CDeviceDriver_Monado_Controller : public vr::ITrackedDeviceServerDriver
{
public:
	CDeviceDriver_Monado_Controller(struct xrt_instance *xinst, struct xrt_device *xdev, enum xrt_hand hand)
	    : m_xdev(xdev), m_hand(hand)
	{
		ovrd_log("Creating Controller %s\n", xdev->str);

		m_handed_controller = true;

		m_emulate_index_controller = debug_get_bool_option_emulate_index_controller();

		if (m_emulate_index_controller) {
			ovrd_log("Emulating Index Controller\n");
		} else {
			ovrd_log("Using Monado Controller profile\n");
		}

		m_unObjectId = vr::k_unTrackedDeviceIndexInvalid;
		m_pose = {};

		// append xrt_hand because SteamVR serial must be unique
		std::stringstream ss;
		ss << "[Monado] " << xdev->str << " " << hand;
		std::string name = ss.str();

		strncpy(m_sSerialNumber, name.c_str(), XRT_DEVICE_NAME_LEN);
		strncpy(m_sModelNumber, name.c_str(), XRT_DEVICE_NAME_LEN);

		strncpy(m_sSerialNumber, name.c_str(), XRT_DEVICE_NAME_LEN);
		strncpy(m_sModelNumber, name.c_str(), XRT_DEVICE_NAME_LEN);

		switch (this->m_xdev->name) {
		case XRT_DEVICE_INDEX_CONTROLLER:
			if (hand == XRT_HAND_LEFT) {
				m_render_model =
				    "{indexcontroller}valve_controller_knu_1_0_"
				    "left";
			}
			if (hand == XRT_HAND_RIGHT) {
				m_render_model =
				    "{indexcontroller}valve_controller_knu_1_0_"
				    "right";
			}
			break;
		case XRT_DEVICE_VIVE_WAND: m_render_model = "vr_controller_vive_1_5"; break;
		case XRT_DEVICE_VIVE_TRACKER_GEN1:
		case XRT_DEVICE_VIVE_TRACKER_GEN2: m_render_model = "{htc}vr_tracker_vive_1_0"; break;
		case XRT_DEVICE_PSMV:
		case XRT_DEVICE_HYDRA:
		case XRT_DEVICE_DAYDREAM:
		case XRT_DEVICE_GENERIC_HMD:
		default: m_render_model = "locator_one_sided"; break;
		}

		ovrd_log("Render model based on Monado: %s\n", m_render_model);

		vr::VRServerDriverHost()->TrackedDeviceAdded(GetSerialNumber().c_str(),
		                                             vr::TrackedDeviceClass_Controller, this);
	}
	virtual ~CDeviceDriver_Monado_Controller() {}

	void
	AddControl(const char *steamvr_control_path,
	           enum xrt_input_name monado_input_name,
	           struct MonadoInputComponent *component)
	{
		enum xrt_input_type monado_input_type = XRT_GET_INPUT_TYPE(monado_input_name);

		SteamVRDriverControlInput in;

		in.monado_input_type = monado_input_type;
		in.steamvr_control_path = steamvr_control_path;
		in.monado_input_name = monado_input_name;
		if (component != NULL) {
			in.component = *component;
		} else {
			in.component.has_component = false;
		}

		if (monado_input_type == XRT_INPUT_TYPE_BOOLEAN) {
			vr::VRDriverInput()->CreateBooleanComponent(m_ulPropertyContainer, steamvr_control_path,
			                                            &in.control_handle);

		} else if (monado_input_type == XRT_INPUT_TYPE_VEC1_MINUS_ONE_TO_ONE) {
			vr::VRDriverInput()->CreateScalarComponent(m_ulPropertyContainer, steamvr_control_path,
			                                           &in.control_handle, vr::VRScalarType_Absolute,
			                                           vr::VRScalarUnits_NormalizedTwoSided);

		} else if (monado_input_type == XRT_INPUT_TYPE_VEC1_ZERO_TO_ONE) {
			vr::VRDriverInput()->CreateScalarComponent(m_ulPropertyContainer, steamvr_control_path,
			                                           &in.control_handle, vr::VRScalarType_Absolute,
			                                           vr::VRScalarUnits_NormalizedOneSided);

		} else if (monado_input_type == XRT_INPUT_TYPE_VEC2_MINUS_ONE_TO_ONE) {
			// 2D values are added as 2 1D values
			// usually those are [-1,1]
			vr::VRDriverInput()->CreateScalarComponent(m_ulPropertyContainer, steamvr_control_path,
			                                           &in.control_handle, vr::VRScalarType_Absolute,
			                                           vr::VRScalarUnits_NormalizedTwoSided);
		}

		m_input_controls.push_back(in);
		ovrd_log("Added input %s\n", steamvr_control_path);
	}

	void
	AddOutputControl(enum xrt_output_name monado_output_name, const char *steamvr_control_path)
	{
		SteamVRDriverControlOutput out;

		// for the future: XRT_GET_OUTPUT_TYPE(monado_output_name);
		enum xrt_output_type monado_output_type = XRT_OUTPUT_TYPE_VIBRATION;

		out.monado_output_type = monado_output_type;
		out.steamvr_control_path = steamvr_control_path;
		out.monado_output_name = monado_output_name;

		vr::VRDriverInput()->CreateHapticComponent(m_ulPropertyContainer, out.steamvr_control_path,
		                                           &out.control_handle);

		m_output_controls.push_back(out);
		ovrd_log("Added output %s\n", steamvr_control_path);
	}
	void
	AddEmulatedIndexControls()
	{
		switch (this->m_xdev->name) {
		case XRT_DEVICE_INDEX_CONTROLLER: {
			AddControl("/input/trigger/value", XRT_INPUT_INDEX_TRIGGER_VALUE, NULL);

			AddControl("/input/trigger/click", XRT_INPUT_INDEX_TRIGGER_CLICK, NULL);
			AddControl("/input/trigger/touch", XRT_INPUT_INDEX_TRIGGER_TOUCH, NULL);


			AddControl("/input/system/click", XRT_INPUT_INDEX_SYSTEM_CLICK, NULL);
			AddControl("/input/system/touch", XRT_INPUT_INDEX_SYSTEM_TOUCH, NULL);

			AddControl("/input/a/click", XRT_INPUT_INDEX_A_CLICK, NULL);
			AddControl("/input/a/touch", XRT_INPUT_INDEX_A_TOUCH, NULL);

			AddControl("/input/b/click", XRT_INPUT_INDEX_B_CLICK, NULL);
			AddControl("/input/b/touch", XRT_INPUT_INDEX_B_TOUCH, NULL);


			AddControl("/input/grip/force", XRT_INPUT_INDEX_SQUEEZE_FORCE, NULL);

			AddControl("/input/grip/value", XRT_INPUT_INDEX_SQUEEZE_VALUE, NULL);

			struct MonadoInputComponent x = {true, true, false};
			struct MonadoInputComponent y = {true, false, true};

			AddControl("/input/thumbstick/click", XRT_INPUT_INDEX_THUMBSTICK_CLICK, NULL);

			AddControl("/input/thumbstick/touch", XRT_INPUT_INDEX_THUMBSTICK_TOUCH, NULL);

			AddControl("/input/thumbstick/x", XRT_INPUT_INDEX_THUMBSTICK, &x);

			AddControl("/input/thumbstick/y", XRT_INPUT_INDEX_THUMBSTICK, &y);


			AddControl("/input/trackpad/force", XRT_INPUT_INDEX_TRACKPAD_FORCE, NULL);

			AddControl("/input/trackpad/touch", XRT_INPUT_INDEX_TRACKPAD_TOUCH, NULL);

			AddControl("/input/trackpad/x", XRT_INPUT_INDEX_TRACKPAD, &x);

			AddControl("/input/trackpad/y", XRT_INPUT_INDEX_TRACKPAD, &y);


			AddOutputControl(XRT_OUTPUT_NAME_INDEX_HAPTIC, "/output/haptic");
		}

		break;
		case XRT_DEVICE_VIVE_WAND: {
			AddControl("/input/trigger/value", XRT_INPUT_VIVE_TRIGGER_VALUE, NULL);

			AddControl("/input/trigger/click", XRT_INPUT_VIVE_TRIGGER_CLICK, NULL);


			AddControl("/input/system/click", XRT_INPUT_VIVE_SYSTEM_CLICK, NULL);

			AddControl("/input/a/click", XRT_INPUT_VIVE_TRACKPAD_CLICK, NULL);

			AddControl("/input/b/click", XRT_INPUT_VIVE_MENU_CLICK, NULL);

			struct MonadoInputComponent x = {true, true, false};
			struct MonadoInputComponent y = {true, false, true};

			AddControl("/input/trackpad/touch", XRT_INPUT_VIVE_TRACKPAD_TOUCH, NULL);

			AddControl("/input/trackpad/x", XRT_INPUT_VIVE_TRACKPAD, &x);

			AddControl("/input/trackpad/y", XRT_INPUT_VIVE_TRACKPAD, &y);

			AddOutputControl(XRT_OUTPUT_NAME_VIVE_HAPTIC, "/output/haptic");
		} break;
		case XRT_DEVICE_PSMV: {

			AddControl("/input/trigger/value", XRT_INPUT_PSMV_TRIGGER_VALUE, NULL);

			AddControl("/input/trigger/click", XRT_INPUT_PSMV_MOVE_CLICK, NULL);

			AddControl("/input/system/click", XRT_INPUT_PSMV_PS_CLICK, NULL);

			AddControl("/input/a/click", XRT_INPUT_PSMV_CROSS_CLICK, NULL);

			AddControl("/input/b/click", XRT_INPUT_PSMV_SQUARE_CLICK, NULL);

			AddOutputControl(XRT_OUTPUT_NAME_PSMV_RUMBLE_VIBRATION, "/output/haptic");
		} break;

		case XRT_DEVICE_TOUCH_CONTROLLER: break;  // TODO
		case XRT_DEVICE_WMR_CONTROLLER: break;    // TODO
		case XRT_DEVICE_XBOX_CONTROLLER: break;   // TODO
		case XRT_DEVICE_VIVE_TRACKER_GEN1: break; // TODO
		case XRT_DEVICE_VIVE_TRACKER_GEN2: break; // TODO

		case XRT_DEVICE_HAND_INTERACTION: break;  // there is no hardware
		case XRT_DEVICE_GO_CONTROLLER: break;     // hardware has no haptics
		case XRT_DEVICE_DAYDREAM: break;          // hardware has no haptics
		case XRT_DEVICE_HYDRA: break;             // hardware has no haptics
		case XRT_DEVICE_SIMPLE_CONTROLLER: break; // shouldn't happen
		case XRT_DEVICE_HAND_TRACKER: break;      // shouldn't happen
		case XRT_DEVICE_GENERIC_HMD:
		case XRT_DEVICE_VIVE_PRO: break; // no
		}
	}

	struct profile_template *
	get_profile_template(enum xrt_device_name device_name)
	{
		for (int i = 0; i < NUM_PROFILE_TEMPLATES; i++) {
			if (profile_templates[i].name == device_name)
				return &profile_templates[i];
		}
		return NULL;
	}

	void
	AddMonadoInput(struct binding_template *b)
	{
		enum xrt_input_name monado_input_name = b->input;
		const char *steamvr_path = b->steamvr_path;

		enum xrt_input_type monado_input_type = XRT_GET_INPUT_TYPE(monado_input_name);

		switch (monado_input_type) {
		case XRT_INPUT_TYPE_BOOLEAN:
		case XRT_INPUT_TYPE_VEC1_MINUS_ONE_TO_ONE:
		case XRT_INPUT_TYPE_VEC1_ZERO_TO_ONE: AddControl(steamvr_path, monado_input_name, NULL); break;

		case XRT_INPUT_TYPE_VEC2_MINUS_ONE_TO_ONE: {
			std::string xpath = std::string(steamvr_path) + std::string("/x");
			std::string ypath = std::string(steamvr_path) + std::string("/y");

			struct MonadoInputComponent x = {true, true, false};
			struct MonadoInputComponent y = {true, false, true};

			AddControl(xpath.c_str(), monado_input_name, &x);
			AddControl(ypath.c_str(), monado_input_name, &y);
		} break;
		case XRT_INPUT_TYPE_POSE:
			//! @todo how to handle poses?
		case XRT_INPUT_TYPE_HAND_TRACKING:
		case XRT_INPUT_TYPE_VEC3_MINUS_ONE_TO_ONE: break;
		}
	}

	void
	AddMonadoControls()
	{
		struct profile_template *p = get_profile_template(m_xdev->name);
		if (!p) {
			ovrd_log("No profile template for %s\n", m_xdev->str);
			return;
		}

		for (size_t i = 0; i < p->num_bindings; i++) {
			struct binding_template *b = &p->bindings[i];

			if (b->input != 0) {
				AddMonadoInput(b);
			}
			if (b->output != 0) {
				AddOutputControl(b->output, b->steamvr_path);
			}
		}
	}

	void
	PoseUpdateThreadFunction()
	{
		ovrd_log("Starting controller pose update thread for %s\n", m_xdev->str);

		while (m_poseUpdating) {
			//! @todo figure out the best pose update rate
			std::this_thread::sleep_for(std::chrono::milliseconds(1));

			if (m_unObjectId != vr::k_unTrackedDeviceIndexInvalid) {
				vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_unObjectId, GetPose(),
				                                                   sizeof(vr::DriverPose_t));
			}
		}

		ovrd_log("Stopping controller pose update thread for %s\n", m_xdev->str);
	}

	vr::EVRInitError
	Activate(vr::TrackedDeviceIndex_t unObjectId)
	{
		ovrd_log("Activating Controller SteamVR[%d]\n", unObjectId);

		if (!m_handed_controller) {
			//! @todo handle trackers etc
			ovrd_log("Unhandled: %s\n", m_xdev->str);
			return vr::VRInitError_Unknown;
		}

		m_unObjectId = unObjectId;

		if (this->m_xdev == NULL) {
			ovrd_log("Error: xdev NULL\n");
			return vr::VRInitError_Init_InterfaceNotFound;
		}

		// clang-format off


		m_ulPropertyContainer = vr::VRProperties()->TrackedDeviceToPropertyContainer(m_unObjectId);

		// return a constant that's not 0 (invalid) or 1 (reserved for Oculus)
		vr::VRProperties()->SetUint64Property(m_ulPropertyContainer, vr::Prop_CurrentUniverseId_Uint64, 2);
		vr::VRProperties()->SetInt32Property(m_ulPropertyContainer, vr::Prop_DeviceClass_Int32, vr::TrackedDeviceClass_Controller);

		if (m_hand == XRT_HAND_LEFT) {
			ovrd_log("Left Controller\n");
			vr::VRProperties()->SetInt32Property(m_ulPropertyContainer, vr::Prop_ControllerRoleHint_Int32, vr::TrackedControllerRole_LeftHand);
		} else if (m_hand == XRT_HAND_RIGHT) {
			ovrd_log("Right Controller\n");
			vr::VRProperties()->SetInt32Property(m_ulPropertyContainer, vr::Prop_ControllerRoleHint_Int32, vr::TrackedControllerRole_RightHand);
		}
		m_pose.poseIsValid = false;
		m_pose.deviceIsConnected = true;
		m_pose.result = vr::TrackingResult_Uninitialized;
		m_pose.willDriftInYaw = !m_xdev->position_tracking_supported;

		if (m_emulate_index_controller) {
			m_input_profile = std::string("{indexcontroller}/input/index_controller_profile.json");
			m_controller_type = "knuckles";
			if (m_hand == XRT_HAND_LEFT) {
				m_render_model = "{indexcontroller}valve_controller_knu_1_0_left";
			} else if (m_hand == XRT_HAND_RIGHT) {
				m_render_model = "{indexcontroller}valve_controller_knu_1_0_right";
			}
		} else {

			struct profile_template *p = get_profile_template(m_xdev->name);

			m_input_profile = std::string("{monado}/input/") + std::string(p->steamvr_input_profile_path);
			m_controller_type = p->steamvr_controller_type;
		}

		ovrd_log("Using input profile %s\n", m_input_profile.c_str());
		ovrd_log("Using render model%s\n", m_render_model);
		vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_InputProfilePath_String, m_input_profile.c_str());
		vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_RenderModelName_String, m_render_model);
		vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_ModelNumber_String, m_xdev->str);

		// clang-format on

		m_input_controls.clear();
		if (m_emulate_index_controller) {
			AddEmulatedIndexControls();
		} else {
			AddMonadoControls();
		}

		ovrd_log("Controller %d activated\n", m_unObjectId);

		m_poseUpdateThread = new std::thread(&CDeviceDriver_Monado_Controller::PoseUpdateThreadFunction, this);
		if (!m_poseUpdateThread) {
			ovrd_log("Unable to create pose updated thread for %s\n", m_xdev->str);
			return vr::VRInitError_Driver_Failed;
		}

		return vr::VRInitError_None;
	}

	void
	Deactivate()
	{
		ovrd_log("deactivate controller\n");
		m_poseUpdating = false;
		m_poseUpdateThread->join();
		m_unObjectId = vr::k_unTrackedDeviceIndexInvalid;
	}

	void
	EnterStandby()
	{
		ovrd_log("standby controller\n");
	}

	void *
	GetComponent(const char *pchComponentNameAndVersion)
	{
		// deprecated API
		// ovrd_log("get controller component %s\n",
		// pchComponentNameAndVersion);
		return NULL;
	}

	/** debug request from a client */
	void
	DebugRequest(const char *pchRequest, char *pchResponseBuffer, uint32_t unResponseBufferSize)
	{
		if (unResponseBufferSize >= 1)
			pchResponseBuffer[0] = 0;
	}

	vr::DriverPose_t
	GetPose()
	{
		// monado predicts pose "now", see xrt_device_get_tracked_pose
		m_pose.poseTimeOffset = 0;

		m_pose.poseIsValid = true;
		m_pose.result = vr::TrackingResult_Running_OK;
		m_pose.deviceIsConnected = true;

		enum xrt_input_name grip_name;

		//! @todo better method to find grip name
		if (m_xdev->name == XRT_DEVICE_VIVE_WAND) {
			grip_name = XRT_INPUT_VIVE_GRIP_POSE;
		} else if (m_xdev->name == XRT_DEVICE_INDEX_CONTROLLER) {
			grip_name = XRT_INPUT_INDEX_GRIP_POSE;
		} else if (m_xdev->name == XRT_DEVICE_PSMV) {
			grip_name = XRT_INPUT_PSMV_GRIP_POSE;
		} else if (m_xdev->name == XRT_DEVICE_DAYDREAM) {
			grip_name = XRT_INPUT_DAYDREAM_POSE;
		} else if (m_xdev->name == XRT_DEVICE_HYDRA) {
			grip_name = XRT_INPUT_HYDRA_POSE;
		} else {
			ovrd_log("Unhandled device name %u\n", m_xdev->name);
			grip_name = XRT_INPUT_GENERIC_HEAD_POSE; // ???
		}

		timepoint_ns now_ns = os_monotonic_get_ns();

		struct xrt_space_relation rel;
		xrt_device_get_tracked_pose(m_xdev, grip_name, now_ns, &rel);

		struct xrt_pose *offset = &m_xdev->tracking_origin->offset;

		struct xrt_space_graph graph = {};
		m_space_graph_add_relation(&graph, &rel);
		m_space_graph_add_pose_if_not_identity(&graph, offset);
		m_space_graph_resolve(&graph, &rel);

		apply_pose(&rel, &m_pose);

#ifdef DUMP_POSE_CONTROLLERS
		ovrd_log("get controller %d pose %f %f %f %f, %f %f %f\n", m_unObjectId, m_pose.qRotation.x,
		         m_pose.qRotation.y, m_pose.qRotation.z, m_pose.qRotation.w, m_pose.vecPosition[0],
		         m_pose.vecPosition[1], m_pose.vecPosition[2]);
#endif

		vr::HmdQuaternion_t identityquat{1, 0, 0, 0};
		m_pose.qWorldFromDriverRotation = identityquat;
		m_pose.qDriverFromHeadRotation = identityquat;
		m_pose.vecDriverFromHeadTranslation[0] = 0.f;
		m_pose.vecDriverFromHeadTranslation[1] = 0.f;
		m_pose.vecDriverFromHeadTranslation[2] = 0.f;

		return m_pose;
	}

	void
	RunFrame()
	{
		m_xdev->update_inputs(m_xdev);


		for (auto in : m_input_controls) {

			// ovrd_log("Update %d: %s\n", i,
			// m_controls[i].steamvr_control_path);

			enum xrt_input_name binding_name = in.monado_input_name;

			struct xrt_input *input = NULL;
			for (uint32_t ii = 0; ii < m_xdev->num_inputs; ii++) {
				if (m_xdev->inputs[ii].name == binding_name) {
					input = &m_xdev->inputs[ii];
					break;
				}
			}

			if (input == NULL) {
				ovrd_log("Input for %s not found!\n", in.steamvr_control_path);
				continue;
			}

			vr::VRInputComponentHandle_t handle = in.control_handle;

			if (in.monado_input_type == XRT_INPUT_TYPE_BOOLEAN) {
				bool state = input->value.boolean;
				vr::VRDriverInput()->UpdateBooleanComponent(handle, state, 0);
				// ovrd_log("Update %s: %d\n",
				// m_controls[i].steamvr_control_path, state);
				// U_LOG_D("Update %s: %d",
				//       m_controls[i].steamvr_control_path,
				//       state);
			}
			if (in.monado_input_type == XRT_INPUT_TYPE_VEC1_MINUS_ONE_TO_ONE ||
			    in.monado_input_type == XRT_INPUT_TYPE_VEC1_ZERO_TO_ONE ||
			    in.monado_input_type == XRT_INPUT_TYPE_VEC2_MINUS_ONE_TO_ONE) {

				float value;
				if (in.component.has_component && in.component.x) {
					value = input->value.vec2.x;
				} else if (in.component.has_component && in.component.y) {
					value = input->value.vec2.y;
				} else {
					value = input->value.vec1.x;
				}

				vr::VRDriverInput()->UpdateScalarComponent(handle, value, 0);
				// ovrd_log("Update %s: %f\n",
				// m_controls[i].steamvr_control_path,
				// state->x);
				// U_LOG_D("Update %s: %f",
				//       m_controls[i].steamvr_control_path,
				//       value);
			}
		}
	}


	vr::VRControllerState_t
	GetControllerState()
	{
		// deprecated API
		vr::VRControllerState_t controllerstate;
		return controllerstate;
	}

	bool
	TriggerHapticPulse(uint32_t unAxisId, uint16_t usPulseDurationMicroseconds)
	{
		// deprecated API
		return false;
	}

	std::string
	GetSerialNumber() const
	{
		ovrd_log("get controller serial number: %s\n", m_sSerialNumber);
		return m_sSerialNumber;
	}


	struct xrt_device *m_xdev;
	vr::DriverPose_t m_pose;
	vr::TrackedDeviceIndex_t m_unObjectId;
	vr::PropertyContainerHandle_t m_ulPropertyContainer;

	bool m_emulate_index_controller = false;

	std::vector<struct SteamVRDriverControlInput> m_input_controls;
	std::vector<struct SteamVRDriverControlOutput> m_output_controls;

private:
	char m_sSerialNumber[XRT_DEVICE_NAME_LEN];
	char m_sModelNumber[XRT_DEVICE_NAME_LEN];

	const char *m_controller_type = NULL;

	const char *m_render_model = NULL;
	enum xrt_hand m_hand;
	bool m_handed_controller;

	std::string m_input_profile;

	bool m_poseUpdating = true;
	std::thread *m_poseUpdateThread = NULL;
};

/*
 *
 * Device driver
 *
 */

class CDeviceDriver_Monado : public vr::ITrackedDeviceServerDriver, public vr::IVRDisplayComponent
{
public:
	CDeviceDriver_Monado(struct xrt_instance *xinst, struct xrt_device *xdev) : m_xdev(xdev)
	{
		//! @todo latency
		m_flSecondsFromVsyncToPhotons = 0.011;

		float ns = (float)m_xdev->hmd->screens->nominal_frame_interval_ns;
		m_flDisplayFrequency = 1. / ns * 1000. * 1000. * 1000.;
		ovrd_log("display frequency from device: %f\n", m_flDisplayFrequency);

		// steamvr can really misbehave when freq is inf or so
		if (m_flDisplayFrequency < 0 || m_flDisplayFrequency > 1000) {
			ovrd_log("Setting display frequency to 60 Hz!\n");
			m_flDisplayFrequency = 60.;
		}

		//! @todo get ipd user setting from monado session
		float ipd_meters = 0.063;
		struct xrt_vec3 ipd_vec = {ipd_meters, 0, 0};

		for (int view = 0; view < 2; view++) {
			xdev->get_view_pose(xdev, &ipd_vec, view, &m_view_pose[view]);
		}

		//! @todo more versatile IPD calculation
		float actual_ipd = -m_view_pose[0].position.x + m_view_pose[1].position.x;

		m_flIPD = actual_ipd;

		ovrd_log("Seconds from Vsync to Photons: %f\n", m_flSecondsFromVsyncToPhotons);
		ovrd_log("Display Frequency: %f\n", m_flDisplayFrequency);
		ovrd_log("IPD: %f\n", m_flIPD);
	};
	virtual ~CDeviceDriver_Monado(){};

	// clang-format off

	// ITrackedDeviceServerDriver
	virtual vr::EVRInitError Activate(vr::TrackedDeviceIndex_t unObjectId);
	virtual void Deactivate();
	virtual void EnterStandby();
	virtual void *GetComponent(const char *pchComponentNameAndVersion);
	virtual void DebugRequest(const char *pchRequest, char *pchResponseBuffer, uint32_t unResponseBufferSize);
	virtual vr::DriverPose_t GetPose();

	// IVRDisplayComponent
	virtual void GetWindowBounds(int32_t *pnX, int32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight);
	virtual bool IsDisplayOnDesktop();
	virtual bool IsDisplayRealDisplay();
	virtual void GetRecommendedRenderTargetSize(uint32_t *pnWidth, uint32_t *pnHeight);
	virtual void GetEyeOutputViewport(vr::EVREye eEye, uint32_t *pnX, uint32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight);
	virtual void GetProjectionRaw(vr::EVREye eEye, float *pfLeft, float *pfRight, float *pfTop, float *pfBottom);
	virtual vr::DistortionCoordinates_t ComputeDistortion(vr::EVREye eEye, float fU, float fV);

	// clang-format on

private:
	struct xrt_device *m_xdev = NULL;

	// clang-format off

	vr::TrackedDeviceIndex_t m_trackedDeviceIndex = 0;
	vr::PropertyContainerHandle_t m_ulPropertyContainer = vr::k_ulInvalidPropertyContainer;

	float m_flSecondsFromVsyncToPhotons = -1;
	float m_flDisplayFrequency = -1;
	float m_flIPD = -1;

	struct xrt_pose m_view_pose[2];

	bool m_poseUpdating = true;
	std::thread *m_poseUpdateThread = NULL;
	virtual void PoseUpdateThreadFunction();

	// clang-format on
};

static void
create_translation_rotation_matrix(struct xrt_pose *pose, struct vr::HmdMatrix34_t *res)
{
	struct xrt_vec3 t = pose->position;
	struct xrt_quat r = pose->orientation;
	res->m[0][0] = (1.0f - 2.0f * (r.y * r.y + r.z * r.z));
	res->m[1][0] = (r.x * r.y + r.z * r.w) * 2.0f;
	res->m[2][0] = (r.x * r.z - r.y * r.w) * 2.0f;
	res->m[0][1] = (r.x * r.y - r.z * r.w) * 2.0f;
	res->m[1][1] = (1.0f - 2.0f * (r.x * r.x + r.z * r.z));
	res->m[2][1] = (r.y * r.z + r.x * r.w) * 2.0f;
	res->m[0][2] = (r.x * r.z + r.y * r.w) * 2.0f;
	res->m[1][2] = (r.y * r.z - r.x * r.w) * 2.0f;
	res->m[2][2] = (1.0f - 2.0f * (r.x * r.x + r.y * r.y));
	res->m[0][3] = t.x;
	res->m[1][3] = t.y;
	res->m[2][3] = t.z;
}

void
CDeviceDriver_Monado::PoseUpdateThreadFunction()
{
	ovrd_log("Starting HMD pose update thread\n");

	while (m_poseUpdating) {
		//! @todo figure out the best pose update rate
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_trackedDeviceIndex, GetPose(),
		                                                   sizeof(vr::DriverPose_t));
	}
	ovrd_log("Stopping HMD pose update thread\n");
}

vr::EVRInitError
CDeviceDriver_Monado::Activate(vr::TrackedDeviceIndex_t unObjectId)
{
	ovrd_log("Activate tracked device %u: %s\n", unObjectId, m_xdev->str);

	m_trackedDeviceIndex = unObjectId;

	// clang-format off

	m_ulPropertyContainer = vr::VRProperties()->TrackedDeviceToPropertyContainer(unObjectId);
	//! @todo: proper serial and model number
	vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_ModelNumber_String, m_xdev->str);
	vr::VRProperties()->SetFloatProperty(m_ulPropertyContainer, vr::Prop_UserIpdMeters_Float, m_flIPD);
	vr::VRProperties()->SetFloatProperty(m_ulPropertyContainer, vr::Prop_UserHeadToEyeDepthMeters_Float, 0.f);
	vr::VRProperties()->SetFloatProperty(m_ulPropertyContainer, vr::Prop_DisplayFrequency_Float, m_flDisplayFrequency);
	vr::VRProperties()->SetFloatProperty( m_ulPropertyContainer, vr::Prop_SecondsFromVsyncToPhotons_Float, m_flSecondsFromVsyncToPhotons);

	// return a constant that's not 0 (invalid) or 1 (reserved for Oculus)
	vr::VRProperties()->SetUint64Property( m_ulPropertyContainer, vr::Prop_CurrentUniverseId_Uint64, 2);

	// avoid "not fullscreen" warnings from vrmonitor
	//vr::VRProperties()->SetBoolProperty(m_ulPropertyContainer, vr::Prop_IsOnDesktop_Bool, false);

	// clang-format on


	//! @todo update when ipd changes
	vr::HmdMatrix34_t left;
	create_translation_rotation_matrix(&m_view_pose[0], &left);
	vr::HmdMatrix34_t right;
	create_translation_rotation_matrix(&m_view_pose[1], &right);
	vr::VRServerDriverHost()->TrackedDeviceDisplayTransformUpdated(m_trackedDeviceIndex, left, right);


	m_poseUpdateThread = new std::thread(&CDeviceDriver_Monado::PoseUpdateThreadFunction, this);
	if (!m_poseUpdateThread) {
		ovrd_log("Unable to create pose updated thread for %s\n", m_xdev->str);
		return vr::VRInitError_Driver_Failed;
	}

	return vr::VRInitError_None;
}

void
CDeviceDriver_Monado::Deactivate()
{
	m_poseUpdating = false;
	m_poseUpdateThread->join();
	ovrd_log("Deactivate\n");
}

void
CDeviceDriver_Monado::EnterStandby()
{
	ovrd_log("Enter Standby\n");
}

void *
CDeviceDriver_Monado::GetComponent(const char *pchComponentNameAndVersion)
{
	// clang-format off
	if (strcmp(pchComponentNameAndVersion, vr::IVRDisplayComponent_Version) == 0) {
		return (vr::IVRDisplayComponent *)this;
	}
	// clang-format on

	return NULL;
}

void
CDeviceDriver_Monado::DebugRequest(const char *pchRequest, char *pchResponseBuffer, uint32_t unResponseBufferSize)
{
	//! @todo
}

static inline vr::HmdQuaternion_t
HmdQuaternion_Init(double w, double x, double y, double z)
{
	vr::HmdQuaternion_t quat;
	quat.w = w;
	quat.x = x;
	quat.y = y;
	quat.z = z;
	return quat;
}

vr::DriverPose_t
CDeviceDriver_Monado::GetPose()
{

	timepoint_ns now_ns = os_monotonic_get_ns();
	struct xrt_space_relation rel;
	xrt_device_get_tracked_pose(m_xdev, XRT_INPUT_GENERIC_HEAD_POSE, now_ns, &rel);

	struct xrt_pose *offset = &m_xdev->tracking_origin->offset;

	struct xrt_space_graph graph = {};
	m_space_graph_add_relation(&graph, &rel);
	m_space_graph_add_pose_if_not_identity(&graph, offset);
	m_space_graph_resolve(&graph, &rel);

	vr::DriverPose_t t = {};


	// monado predicts pose "now", see xrt_device_get_tracked_pose
	t.poseTimeOffset = 0;

	//! @todo: Monado head model?
	t.shouldApplyHeadModel = !m_xdev->position_tracking_supported;
	t.willDriftInYaw = !m_xdev->position_tracking_supported;

	t.qWorldFromDriverRotation = HmdQuaternion_Init(1, 0, 0, 0);
	t.qDriverFromHeadRotation = HmdQuaternion_Init(1, 0, 0, 0);

	t.poseIsValid = rel.relation_flags & XRT_SPACE_RELATION_ORIENTATION_VALID_BIT;
	t.result = vr::TrackingResult_Running_OK;
	t.deviceIsConnected = true;

	apply_pose(&rel, &t);

#ifdef DUMP_POSE
	ovrd_log("get hmd pose %f %f %f %f, %f %f %f\n", t.qRotation.x, t.qRotation.y, t.qRotation.z, t.qRotation.w,
	         t.vecPosition[0], t.vecPosition[1], t.vecPosition[2]);
#endif

	//! @todo
	// copy_vec3(&rel.angular_velocity, t.vecAngularVelocity);
	// copy_vec3(&rel.angular_acceleration, t.vecAngularAcceleration);

	// ovrd_log("Vel: %f %f %f\n", t.vecAngularVelocity[0],
	// t.vecAngularVelocity[1], t.vecAngularVelocity[2]); ovrd_log("Accel:
	// %f %f %f\n", t.vecAngularAcceleration[0],
	// t.vecAngularAcceleration[1], t.vecAngularAcceleration[2]);

	return t;
}

void
CDeviceDriver_Monado::GetWindowBounds(int32_t *pnX, int32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight)
{
	// offset in extended mode, e.g. to the right of a 1920x1080 monitor
	*pnX = 1920;
	*pnY = 0;

	*pnWidth = m_xdev->hmd->screens[0].w_pixels;
	*pnHeight = m_xdev->hmd->screens[0].h_pixels;
	;

	ovrd_log("Window Bounds: %dx%d\n", *pnWidth, *pnHeight);
}

bool
CDeviceDriver_Monado::IsDisplayOnDesktop()
{
	return false;
}

bool
CDeviceDriver_Monado::IsDisplayRealDisplay()
{
	return true;
}

void
CDeviceDriver_Monado::GetRecommendedRenderTargetSize(uint32_t *pnWidth, uint32_t *pnHeight)
{
	int scale = debug_get_num_option_scale_percentage();
	float fscale = (float)scale / 100.f;



	*pnWidth = m_xdev->hmd->screens[0].w_pixels * fscale;
	*pnHeight = m_xdev->hmd->screens[0].h_pixels * fscale;

	ovrd_log("Render Target Size: %dx%d (%fx)\n", *pnWidth, *pnHeight, fscale);
}

void
CDeviceDriver_Monado::GetEyeOutputViewport(
    vr::EVREye eEye, uint32_t *pnX, uint32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight)
{
	*pnWidth = m_xdev->hmd->views[eEye].viewport.w_pixels;
	*pnHeight = m_xdev->hmd->views[eEye].viewport.h_pixels;

	*pnX = m_xdev->hmd->views[eEye].viewport.x_pixels;
	*pnY = m_xdev->hmd->views[eEye].viewport.y_pixels;

	ovrd_log("Output Viewport for eye %d: %dx%d offset %dx%d\n", eEye, *pnWidth, *pnHeight, *pnX, *pnY);
}

void
CDeviceDriver_Monado::GetProjectionRaw(vr::EVREye eEye, float *pfLeft, float *pfRight, float *pfTop, float *pfBottom)
{
	*pfLeft = tanf(m_xdev->hmd->views[eEye].fov.angle_left);
	*pfRight = tanf(m_xdev->hmd->views[eEye].fov.angle_right);
	*pfTop = tanf(-m_xdev->hmd->views[eEye].fov.angle_up);
	*pfBottom = tanf(-m_xdev->hmd->views[eEye].fov.angle_down);
	ovrd_log("Projection Raw: L%f R%f T%f B%f\n", *pfLeft, *pfRight, *pfTop, *pfBottom);
}

vr::DistortionCoordinates_t
CDeviceDriver_Monado::ComputeDistortion(vr::EVREye eEye, float fU, float fV)
{
	/** Used to return the post-distortion UVs for each color channel.
	 * UVs range from 0 to 1 with 0,0 in the upper left corner of the
	 * source render target. The 0,0 to 1,1 range covers a single eye. */

	struct xrt_vec2 *rot = m_xdev->hmd->views[eEye].rot.vecs;

	// multiply 2x2 rotation matrix with fU, fV scaled to [-1, 1]
	float U = rot[0].x * (fU * 2 - 1) + rot[0].y * (fV * 2 - 1);
	float V = rot[1].x * (fU * 2 - 1) + rot[1].y * (fV * 2 - 1);

	// scale U, V back to [0, 1]
	U = (U + 1) / 2;
	V = (V + 1) / 2;

	struct xrt_uv_triplet d;

	if (!m_xdev->compute_distortion(m_xdev, eEye, U, V, &d)) {
		ovrd_log("Failed to compute distortion for view %d at %f,%f!\n", eEye, U, V);

		vr::DistortionCoordinates_t coordinates;
		coordinates.rfBlue[0] = U;
		coordinates.rfBlue[1] = V;
		coordinates.rfGreen[0] = U;
		coordinates.rfGreen[1] = V;
		coordinates.rfRed[0] = U;
		coordinates.rfRed[1] = V;
		return coordinates;
	}

	vr::DistortionCoordinates_t coordinates;
	coordinates.rfRed[0] = d.r.x;
	coordinates.rfRed[1] = d.r.y;
	coordinates.rfGreen[0] = d.g.x;
	coordinates.rfGreen[1] = d.g.y;
	coordinates.rfBlue[0] = d.b.x;
	coordinates.rfBlue[1] = d.b.y;

	// ovrd_log("Computed distortion for view %d at %f,%f -> %f,%f | %f,%f |
	// %f,%f!\n", eEye, U, V, d.r.x, d.r.y, d.g.x, d.g.y, d.b.x, d.b.y);

	return coordinates;
}


/*
 *
 * Device driver server
 *
 */

class CServerDriver_Monado : public vr::IServerTrackedDeviceProvider
{
public:
	CServerDriver_Monado() : m_MonadoDeviceDriver(NULL) {}

	// clang-format off
	virtual ~CServerDriver_Monado() {};
	virtual vr::EVRInitError Init(vr::IVRDriverContext *pDriverContext);
	virtual void Cleanup();
	virtual const char *const * GetInterfaceVersions() { return vr::k_InterfaceVersions; }
	virtual void RunFrame();
	virtual bool ShouldBlockStandbyMode() { return false; }
	virtual void EnterStandby() {}
	virtual void LeaveStandby() {}
	virtual void HandleHapticEvent(vr::VREvent_t *event);
	// clang-format on

private:
	struct xrt_instance *m_xinst = NULL;
	struct xrt_device *m_xhmd = NULL;

	CDeviceDriver_Monado *m_MonadoDeviceDriver = NULL;
	CDeviceDriver_Monado_Controller *m_left = NULL;
	CDeviceDriver_Monado_Controller *m_right = NULL;
};

CServerDriver_Monado g_serverDriverMonado;

#define NUM_XDEVS 16

vr::EVRInitError
CServerDriver_Monado::Init(vr::IVRDriverContext *pDriverContext)
{

	VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext);
	ovrd_log_init(vr::VRDriverLog());

	ovrd_log("Initializing Monado driver\n");

	//! @todo instance initialization is difficult to replicate

	int ret;
	ret = xrt_instance_create(NULL, &m_xinst);
	if (ret < 0) {
		ovrd_log("Failed to create instance\n");
		return vr::VRInitError_Init_HmdNotFound;
	}

	struct xrt_device *xdevs[NUM_XDEVS] = {0};

	ret = xrt_instance_select(m_xinst, xdevs, NUM_XDEVS);

	int head, left, right;

	u_device_assign_xdev_roles(xdevs, NUM_XDEVS, &head, &left, &right);

	if (ret < 0 || head == XRT_DEVICE_ROLE_UNASSIGNED) {
		ovrd_log("Failed to select HMD\n");
		xrt_instance_destroy(&m_xinst);
		return vr::VRInitError_Init_HmdNotFound;
	}

	m_xhmd = xdevs[head];

	ovrd_log("Selected HMD %s\n", m_xhmd->str);
	m_MonadoDeviceDriver = new CDeviceDriver_Monado(m_xinst, m_xhmd);
	//! @todo provide a serial number
	vr::VRServerDriverHost()->TrackedDeviceAdded(m_xhmd->str, vr::TrackedDeviceClass_HMD, m_MonadoDeviceDriver);

	struct xrt_device *left_xdev = left == XRT_DEVICE_ROLE_UNASSIGNED ? NULL : xdevs[left];
	struct xrt_device *right_xdev = right == XRT_DEVICE_ROLE_UNASSIGNED ? NULL : xdevs[right];
	// use steamvr room setup instead
	struct xrt_vec3 offset = {0, 0, 0};
	u_device_setup_tracking_origins(m_xhmd, left_xdev, right_xdev, &offset);

	if (left_xdev) {
		m_left = new CDeviceDriver_Monado_Controller(m_xinst, left_xdev, XRT_HAND_LEFT);
		ovrd_log("Added left Controller: %s\n", left_xdev->str);
	}
	if (right_xdev) {
		m_right = new CDeviceDriver_Monado_Controller(m_xinst, right_xdev, XRT_HAND_RIGHT);
		ovrd_log("Added right Controller: %s\n", right_xdev->str);
	}

	return vr::VRInitError_None;
}

void
CServerDriver_Monado::Cleanup()
{
	if (m_MonadoDeviceDriver != NULL) {
		delete m_MonadoDeviceDriver;
		m_MonadoDeviceDriver = NULL;
	}

	if (m_xhmd) {
		xrt_device_destroy(&m_xhmd);
	}

	if (m_left) {
		xrt_device_destroy(&m_left->m_xdev);
	}
	if (m_right) {
		xrt_device_destroy(&m_right->m_xdev);
	}

	if (m_xinst) {
		xrt_instance_destroy(&m_xinst);
	}

	return;
}

void
CServerDriver_Monado::HandleHapticEvent(vr::VREvent_t *event)
{
	float freq = event->data.hapticVibration.fFrequency;
	float amp = event->data.hapticVibration.fAmplitude;
	float duration = event->data.hapticVibration.fDurationSeconds;

	ovrd_log("Haptic vibration %fs %fHz %famp\n", duration, freq, amp);

	CDeviceDriver_Monado_Controller *controller = NULL;

	if (m_left && m_left->m_ulPropertyContainer == event->data.hapticVibration.containerHandle) {
		controller = m_left;
		ovrd_log("Haptic vibration left\n");
	} else if (m_right && m_right->m_ulPropertyContainer == event->data.hapticVibration.containerHandle) {
		controller = m_right;
		ovrd_log("Haptic vibration right\n");
	} else {
		ovrd_log("Haptic vibration ignored\n");
		return;
	}

	union xrt_output_value out;
	out.vibration.amplitude = amp;
	if (duration > 0.00001) {
		out.vibration.duration = duration * 1000. * 1000. * 1000.;
	} else {
		out.vibration.duration = XRT_MIN_HAPTIC_DURATION;
	}
	out.vibration.frequency = freq;

	if (controller->m_output_controls.size() < 1) {
		ovrd_log("Controller %s has no outputs\n", controller->m_xdev->str);
		return;
	}

	// TODO: controllers with more than 1 haptic motors
	SteamVRDriverControlOutput *control = &controller->m_output_controls.at(0);

	enum xrt_output_name name = control->monado_output_name;
	ovrd_log("Haptic vibration %s, %d\n", control->steamvr_control_path, name);
	controller->m_xdev->set_output(controller->m_xdev, name, &out);
}

void
CServerDriver_Monado::RunFrame()
{
	if (m_left) {
		m_left->RunFrame();
	}
	if (m_right) {
		m_right->RunFrame();
	}

	// https://github.com/ValveSoftware/openvr/issues/719#issuecomment-358038640
	struct vr::VREvent_t event;
	while (vr::VRServerDriverHost()->PollNextEvent(&event, sizeof(struct vr::VREvent_t))) {
		switch (event.eventType) {
		case vr::VREvent_Input_HapticVibration: HandleHapticEvent(&event); break;
		case vr::VREvent_PropertyChanged:
			// ovrd_log("Property changed\n");
			break;
		case vr::VREvent_TrackedDeviceActivated:
			ovrd_log("Device activated %d\n", event.trackedDeviceIndex);
			break;
		case vr::VREvent_TrackedDeviceUserInteractionStarted:
			ovrd_log("Device interaction started %d\n", event.trackedDeviceIndex);
			break;
		case vr::VREvent_IpdChanged: ovrd_log("ipd changed to %fm\n", event.data.ipd.ipdMeters); break;
		case vr::VREvent_ActionBindingReloaded: ovrd_log("action binding reloaded\n"); break;
		case vr::VREvent_StatusUpdate: ovrd_log("EVRState: %d\n", event.data.status.statusState); break;

		case vr::VREvent_TrackedDeviceRoleChanged:
			// device roles are for legacy input
		case vr::VREvent_ChaperoneUniverseHasChanged:
		case vr::VREvent_ProcessQuit:
		case vr::VREvent_QuitAcknowledged:
		case vr::VREvent_ProcessDisconnected:
		case vr::VREvent_ProcessConnected:
		case vr::VREvent_DashboardActivated:
		case vr::VREvent_DashboardDeactivated:
		case vr::VREvent_Compositor_ChaperoneBoundsShown:
		case vr::VREvent_Compositor_ChaperoneBoundsHidden: break;

		default: ovrd_log("Unhandled Event: %d\n", event.eventType);
		}
	}
}


/*
 *
 * Whatchdog code
 *
 */

class CWatchdogDriver_Monado : public vr::IVRWatchdogProvider
{
public:
	CWatchdogDriver_Monado()
	{
		ovrd_log("Created watchdog object\n");
		m_pWatchdogThread = nullptr;
	}

	// clang-format off
	virtual ~CWatchdogDriver_Monado() {};
	virtual vr::EVRInitError Init(vr::IVRDriverContext *pDriverContext);
	virtual void Cleanup();
	// clang-format on

private:
	std::thread *m_pWatchdogThread;
};

CWatchdogDriver_Monado g_watchdogDriverMonado;
bool g_bExiting = false;

void
WatchdogThreadFunction()
{
	while (!g_bExiting) {
#if defined(_WINDOWS)
		// on windows send the event when the Y key is pressed.
		if ((0x01 & GetAsyncKeyState('Y')) != 0) {
			// Y key was pressed.
			vr::VRWatchdogHost()->WatchdogWakeUp();
		}
		std::this_thread::sleep_for(std::chrono::microseconds(500));
#else
		ovrd_log("Watchdog wakeup\n");
		// for the other platforms, just send one every five seconds
		std::this_thread::sleep_for(std::chrono::seconds(1));
		vr::VRWatchdogHost()->WatchdogWakeUp();
#endif
	}

	ovrd_log("Watchdog exit\n");
}

vr::EVRInitError
CWatchdogDriver_Monado::Init(vr::IVRDriverContext *pDriverContext)
{
	VR_INIT_WATCHDOG_DRIVER_CONTEXT(pDriverContext);
	ovrd_log_init(vr::VRDriverLog());

	// Watchdog mode on Windows starts a thread that listens for the 'Y' key
	// on the keyboard to be pressed. A real driver should wait for a system
	// button event or something else from the the hardware that signals
	// that the VR system should start up.
	g_bExiting = false;

	ovrd_log("starting watchdog thread\n");

	m_pWatchdogThread = new std::thread(WatchdogThreadFunction);
	if (!m_pWatchdogThread) {
		ovrd_log("Unable to create watchdog thread\n");
		return vr::VRInitError_Driver_Failed;
	}

	return vr::VRInitError_None;
}

void
CWatchdogDriver_Monado::Cleanup()
{
	g_bExiting = true;
	if (m_pWatchdogThread) {
		m_pWatchdogThread->join();
		delete m_pWatchdogThread;
		m_pWatchdogThread = nullptr;
	}

	ovrd_log_cleanup();
}


/*
 *
 * 'Exported' functions.
 *
 */

void *
ovrd_hmd_driver_impl(const char *pInterfaceName, int *pReturnCode)
{
	// clang-format off
	if (0 == strcmp(vr::IServerTrackedDeviceProvider_Version, pInterfaceName)) {
		return &g_serverDriverMonado;
	}
	if (0 == strcmp(vr::IVRWatchdogProvider_Version, pInterfaceName)) {
		return &g_watchdogDriverMonado;
	}
	// clang-format on

	ovrd_log("Unimplemented interface: %s\n", pInterfaceName);

	if (pReturnCode) {
		*pReturnCode = vr::VRInitError_Init_InterfaceNotFound;
	}

	return NULL;
}

#pragma GCC diagnostic pop
