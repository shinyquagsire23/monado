// Copyright 2023, Shawn Wallace
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief SteamVR driver context - implements xrt_tracking_origin and IVRDriverContext.
 * @author Shawn Wallace <yungwallace@live.com>
 * @ingroup drv_steamvr_lh
 */

#pragma once

#include <unordered_map>
#include <memory>
#include <optional>
#include <chrono>
#include <deque>
#include <mutex>

#include "openvr_driver.h"

#include "settings.hpp"
#include "resources.hpp"
#include "iobuffer.hpp"
#include "driver_manager.hpp"
#include "server.hpp"
#include "blockqueue.hpp"
#include "paths.hpp"

#include "xrt/xrt_tracking.h"

struct xrt_input;
class Device;
class Context final : public xrt_tracking_origin,
                      public vr::IVRDriverContext,
                      public vr::IVRServerDriverHost,
                      public vr::IVRDriverInput,
                      public vr::IVRProperties,
                      public vr::IVRDriverLog,
                      public std::enable_shared_from_this<Context>

{
	Settings settings;
	Resources resources;
	IOBuffer iobuf;
	DriverManager man;
	Server server;
	BlockQueue blockqueue;
	Paths paths;

	uint64_t current_frame{0};

	std::unordered_map<vr::VRInputComponentHandle_t, xrt_input *> handle_to_input;
	struct Vec2Components
	{
		vr::VRInputComponentHandle_t x;
		vr::VRInputComponentHandle_t y;
	};
	std::unordered_map<vr::VRInputComponentHandle_t, Vec2Components *> vec2_inputs;
	std::unordered_map<xrt_input *, std::unique_ptr<Vec2Components>> vec2_input_to_components;

	struct Event
	{
		std::chrono::steady_clock::time_point insert_time;
		vr::VREvent_t inner;
	};
	std::deque<Event> events;
	std::mutex event_queue_mut;

	Device *
	prop_container_to_device(vr::PropertyContainerHandle_t handle);

	vr::EVRInputError
	create_component_common(vr::PropertyContainerHandle_t container,
	                        const char *name,
	                        vr::VRInputComponentHandle_t *handle);

	xrt_input *
	update_component_common(vr::VRInputComponentHandle_t handle,
	                        double offset,
	                        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

	bool
	setup_hmd(const char *serial, vr::ITrackedDeviceServerDriver *driver);

	bool
	setup_controller(const char *serial, vr::ITrackedDeviceServerDriver *driver);
	vr::IServerTrackedDeviceProvider *provider;

protected:
	Context(const std::string &steam_install, const std::string &steamvr_install, u_logging_level level);

public:
	// These are owned by monado, context is destroyed when these are destroyed
	class HmdDevice *hmd{nullptr};
	class ControllerDevice *controller[2]{nullptr, nullptr};
	const u_logging_level log_level;

	~Context();

	[[nodiscard]] static std::shared_ptr<Context>
	create(const std::string &steam_install,
	       const std::string &steamvr_install,
	       vr::IServerTrackedDeviceProvider *p);

	void
	maybe_run_frame(uint64_t new_frame);

	void
	add_haptic_event(vr::VREvent_HapticVibration_t event);

	void
	Log(const char *pchLogMessage) override;

	/***** IVRDriverContext methods *****/

	void *
	GetGenericInterface(const char *pchInterfaceVersion, vr::EVRInitError *peError) override;

	vr::DriverHandle_t
	GetDriverHandle() override;

	/***** IVRServerDriverHost methods *****/
	bool
	TrackedDeviceAdded(const char *pchDeviceSerialNumber,
	                   vr::ETrackedDeviceClass eDeviceClass,
	                   vr::ITrackedDeviceServerDriver *pDriver) override;

	void
	TrackedDevicePoseUpdated(uint32_t unWhichDevice,
	                         const vr::DriverPose_t &newPose,
	                         uint32_t unPoseStructSize) override;

	void
	VsyncEvent(double vsyncTimeOffsetSeconds) override;

	void
	VendorSpecificEvent(uint32_t unWhichDevice,
	                    vr::EVREventType eventType,
	                    const vr::VREvent_Data_t &eventData,
	                    double eventTimeOffset) override;

	bool
	IsExiting() override;

	bool
	PollNextEvent(vr::VREvent_t *pEvent, uint32_t uncbVREvent) override;

	void
	GetRawTrackedDevicePoses(float fPredictedSecondsFromNow,
	                         vr::TrackedDevicePose_t *pTrackedDevicePoseArray,
	                         uint32_t unTrackedDevicePoseArrayCount) override;

	void
	RequestRestart(const char *pchLocalizedReason,
	               const char *pchExecutableToStart,
	               const char *pchArguments,
	               const char *pchWorkingDirectory) override;

	uint32_t
	GetFrameTimings(vr::Compositor_FrameTiming *pTiming, uint32_t nFrames) override;

	void
	SetDisplayEyeToHead(uint32_t unWhichDevice,
	                    const vr::HmdMatrix34_t &eyeToHeadLeft,
	                    const vr::HmdMatrix34_t &eyeToHeadRight) override;

	void
	SetDisplayProjectionRaw(uint32_t unWhichDevice,
	                        const vr::HmdRect2_t &eyeLeft,
	                        const vr::HmdRect2_t &eyeRight) override;

	void
	SetRecommendedRenderTargetSize(uint32_t unWhichDevice, uint32_t nWidth, uint32_t nHeight) override;

	/***** IVRDriverInput methods *****/

	vr::EVRInputError
	CreateBooleanComponent(vr::PropertyContainerHandle_t ulContainer,
	                       const char *pchName,
	                       vr::VRInputComponentHandle_t *pHandle) override;

	vr::EVRInputError
	UpdateBooleanComponent(vr::VRInputComponentHandle_t ulComponent, bool bNewValue, double fTimeOffset) override;

	vr::EVRInputError
	CreateScalarComponent(vr::PropertyContainerHandle_t ulContainer,
	                      const char *pchName,
	                      vr::VRInputComponentHandle_t *pHandle,
	                      vr::EVRScalarType eType,
	                      vr::EVRScalarUnits eUnits) override;

	vr::EVRInputError
	UpdateScalarComponent(vr::VRInputComponentHandle_t ulComponent, float fNewValue, double fTimeOffset) override;

	vr::EVRInputError
	CreateHapticComponent(vr::PropertyContainerHandle_t ulContainer,
	                      const char *pchName,
	                      vr::VRInputComponentHandle_t *pHandle) override;

	vr::EVRInputError
	CreateSkeletonComponent(vr::PropertyContainerHandle_t ulContainer,
	                        const char *pchName,
	                        const char *pchSkeletonPath,
	                        const char *pchBasePosePath,
	                        vr::EVRSkeletalTrackingLevel eSkeletalTrackingLevel,
	                        const vr::VRBoneTransform_t *pGripLimitTransforms,
	                        uint32_t unGripLimitTransformCount,
	                        vr::VRInputComponentHandle_t *pHandle) override;

	vr::EVRInputError
	UpdateSkeletonComponent(vr::VRInputComponentHandle_t ulComponent,
	                        vr::EVRSkeletalMotionRange eMotionRange,
	                        const vr::VRBoneTransform_t *pTransforms,
	                        uint32_t unTransformCount) override;

	/***** IVRProperties methods *****/

	vr::ETrackedPropertyError
	ReadPropertyBatch(vr::PropertyContainerHandle_t ulContainerHandle,
	                  vr::PropertyRead_t *pBatch,
	                  uint32_t unBatchEntryCount) override;

	vr::ETrackedPropertyError
	WritePropertyBatch(vr::PropertyContainerHandle_t ulContainerHandle,
	                   vr::PropertyWrite_t *pBatch,
	                   uint32_t unBatchEntryCount) override;

	const char *
	GetPropErrorNameFromEnum(vr::ETrackedPropertyError error) override;

	vr::PropertyContainerHandle_t
	TrackedDeviceToPropertyContainer(vr::TrackedDeviceIndex_t nDevice) override;
};
