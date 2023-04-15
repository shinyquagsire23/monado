// Copyright 2023, Shawn Wallace
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief SteamVR driver context implementation and entrypoint.
 * @author Shawn Wallace <yungwallace@live.com>
 * @ingroup drv_steamvr_lh
 */

#include <cstring>
#include <dlfcn.h>
#include <memory>
#include <cmath>
#include <unordered_map>
#include <string_view>
#include <filesystem>
#include <istream>

#include "openvr_driver.h"
#include "vdf_parser.hpp"
#include "steamvr_lh_interface.h"
#include "interfaces/context.hpp"
#include "device.hpp"
#include "util/u_device.h"

namespace {
DEBUG_GET_ONCE_LOG_OPTION(lh_log, "LIGHTHOUSE_LOG", U_LOGGING_INFO);

// ~/.steam/root is a symlink to where the Steam root is
const std::string STEAM_INSTALL_DIR = std::string(getenv("HOME")) + "/.steam/root";
constexpr auto STEAMVR_APPID = "250820";

// Parse libraryfolder.vdf to find where SteamVR is installed
std::string
find_steamvr_install()
{
	using namespace tyti;
	std::ifstream file(STEAM_INSTALL_DIR + "/steamapps/libraryfolders.vdf");
	auto root = vdf::read(file);
	assert(root.name == "libraryfolders");
	for (auto &[_, child] : root.children) {
		U_LOG_D("Found library folder %s", child->attribs["path"].c_str());
		std::shared_ptr<vdf::object> apps = child->children["apps"];
		for (auto &[appid, _] : apps->attribs) {
			if (appid == STEAMVR_APPID) {
				return child->attribs["path"] + "/steamapps/common/SteamVR";
			}
		}
	}
	return std::string();
}

} // namespace

#define CTX_ERR(...) U_LOG_IFL_E(log_level, __VA_ARGS__)
#define CTX_WARN(...) U_LOG_IFL_E(log_level, __VA_ARGS__)
#define CTX_INFO(...) U_LOG_IFL_I(log_level, __VA_ARGS__)
#define CTX_TRACE(...) U_LOG_IFL_T(log_level, __VA_ARGS__)
#define CTX_DEBUG(...) U_LOG_IFL_D(log_level, __VA_ARGS__)

/**
 * Since only the devices will live after our get_devices function is called, we make our Context
 * a shared ptr that is owned by the devices that exist, so that it is also cleaned up by the
 * devices that exist when they are all destroyed.
 */
std::shared_ptr<Context>
Context::create(const std::string &steam_install,
                const std::string &steamvr_install,
                vr::IServerTrackedDeviceProvider *p)
{
	// xrt_tracking_origin initialization
	Context *c = new Context(steam_install, steamvr_install, debug_get_log_option_lh_log());
	c->provider = p;
	std::strncpy(c->name, "SteamVR Lighthouse Tracking", XRT_TRACKING_NAME_LEN);
	c->type = XRT_TRACKING_TYPE_LIGHTHOUSE;
	c->offset = XRT_POSE_IDENTITY;
	return std::shared_ptr<Context>(c);
}

Context::Context(const std::string &steam_install, const std::string &steamvr_install, u_logging_level level)
    : settings(steam_install, steamvr_install), resources(level, steamvr_install), log_level(level)
{}

Context::~Context()
{
	provider->Cleanup();
}

/***** IVRDriverContext methods *****/

void *
Context::GetGenericInterface(const char *pchInterfaceVersion, vr::EVRInitError *peError)
{
#define MATCH_INTERFACE(version, interface)                                                                            \
	if (std::strcmp(pchInterfaceVersion, version) == 0) {                                                          \
		return interface;                                                                                      \
	}
#define MATCH_INTERFACE_THIS(interface) MATCH_INTERFACE(interface##_Version, static_cast<interface *>(this))

	// Known interfaces
	MATCH_INTERFACE_THIS(vr::IVRServerDriverHost);
	MATCH_INTERFACE_THIS(vr::IVRDriverInput);
	MATCH_INTERFACE_THIS(vr::IVRProperties);
	MATCH_INTERFACE_THIS(vr::IVRDriverLog);
	MATCH_INTERFACE(vr::IVRSettings_Version, &settings);
	MATCH_INTERFACE(vr::IVRResources_Version, &resources);
	MATCH_INTERFACE(vr::IVRIOBuffer_Version, &iobuf);
	MATCH_INTERFACE(vr::IVRDriverManager_Version, &man);
	MATCH_INTERFACE(vr::IVRBlockQueue_Version, &blockqueue);
	MATCH_INTERFACE(vr::IVRPaths_Version, &paths);

	// Internal interfaces
	MATCH_INTERFACE("IVRServer_XXX", &server);
	return nullptr;
}

vr::DriverHandle_t
Context::GetDriverHandle()
{
	return 1;
}


/***** IVRServerDriverHost methods *****/

bool
Context::setup_hmd(const char *serial, vr::ITrackedDeviceServerDriver *driver)
{
	this->hmd = new HmdDevice(DeviceBuilder{this->shared_from_this(), driver, serial, STEAM_INSTALL_DIR});
#define VERIFY(expr, msg)                                                                                              \
	if (!(expr)) {                                                                                                 \
		CTX_ERR("Activating HMD failed: %s", msg);                                                             \
		delete this->hmd;                                                                                      \
		this->hmd = nullptr;                                                                                   \
		return false;                                                                                          \
	}
	vr::EVRInitError err = driver->Activate(0);
	VERIFY(err == vr::VRInitError_None, std::to_string(err).c_str());

	auto *display = static_cast<vr::IVRDisplayComponent *>(driver->GetComponent(vr::IVRDisplayComponent_Version));
	VERIFY(display, "IVRDisplayComponent is null");
#undef VERIFY

	auto hmd_parts = std::make_unique<HmdDevice::Parts>();
	for (size_t idx = 0; idx < 2; ++idx) {
		vr::EVREye eye = (idx == 0) ? vr::Eye_Left : vr::Eye_Right;
		xrt_view &view = hmd_parts->base.views[idx];

		display->GetEyeOutputViewport(eye, &view.viewport.x_pixels, &view.viewport.y_pixels,
		                              &view.viewport.w_pixels, &view.viewport.h_pixels);

		view.display.w_pixels = view.viewport.w_pixels;
		view.display.h_pixels = view.viewport.h_pixels;
		view.rot = u_device_rotation_ident;
	}

	hmd_parts->base.screens[0].w_pixels =
	    hmd_parts->base.views[0].display.w_pixels + hmd_parts->base.views[1].display.w_pixels;
	hmd_parts->base.screens[0].h_pixels = hmd_parts->base.views[0].display.h_pixels;
	// nominal frame interval will be set when lighthouse gives us the display frequency
	// see HmdDevice::handle_property_write

	hmd_parts->base.blend_modes[0] = XRT_BLEND_MODE_OPAQUE;
	hmd_parts->base.blend_mode_count = 1;

	auto &distortion = hmd_parts->base.distortion;
	distortion.models = XRT_DISTORTION_MODEL_COMPUTE;
	distortion.preferred = XRT_DISTORTION_MODEL_COMPUTE;
	for (size_t idx = 0; idx < 2; ++idx) {
		xrt_fov &fov = distortion.fov[idx];
		float tan_left, tan_right, tan_top, tan_bottom;
		display->GetProjectionRaw((vr::EVREye)idx, &tan_left, &tan_right, &tan_top, &tan_bottom);
		fov.angle_left = atanf(tan_left);
		fov.angle_right = atanf(tan_right);
		fov.angle_up = atanf(tan_bottom);
		fov.angle_down = atanf(tan_top);
	}

	hmd_parts->display = display;
	hmd->set_hmd_parts(std::move(hmd_parts));
	return true;
}

bool
Context::setup_controller(const char *serial, vr::ITrackedDeviceServerDriver *driver)
{
	if (controller[0] && controller[1]) {
		CTX_WARN("Attempted to activate more than two controllers - this is unsupported");
		return false;
	}
	size_t device_idx = (controller[0]) ? 2 : 1;
	auto &dev = controller[device_idx - 1];
	dev = new ControllerDevice(device_idx + 1,
	                           DeviceBuilder{this->shared_from_this(), driver, serial, STEAM_INSTALL_DIR});

	vr::EVRInitError err = driver->Activate(device_idx);
	if (err != vr::VRInitError_None) {
		CTX_ERR("Activating controller failed: error %u", err);
		return false;
	}

	return true;
}

void
Context::maybe_run_frame(uint64_t new_frame)
{
	if (new_frame > current_frame) {
		++current_frame;
		provider->RunFrame();
	}
}
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
bool
Context::TrackedDeviceAdded(const char *pchDeviceSerialNumber,
                            vr::ETrackedDeviceClass eDeviceClass,
                            vr::ITrackedDeviceServerDriver *pDriver)
{
	CTX_INFO("New device added: %s", pchDeviceSerialNumber);
	switch (eDeviceClass) {
	case vr::TrackedDeviceClass_HMD: {
		return setup_hmd(pchDeviceSerialNumber, pDriver);
		break;
	}
	case vr::TrackedDeviceClass_Controller: {
		return setup_controller(pchDeviceSerialNumber, pDriver);
		break;
	}
	default: {
		CTX_WARN("Attempted to add unsupported device class: %u", eDeviceClass);
		return false;
	}
	}
}

void
Context::TrackedDevicePoseUpdated(uint32_t unWhichDevice, const vr::DriverPose_t &newPose, uint32_t unPoseStructSize)
{
	assert(sizeof(newPose) == unPoseStructSize);
	if (unWhichDevice > 2)
		return;
	Device *dev = (unWhichDevice == 0) ? static_cast<Device *>(this->hmd)
	                                   : static_cast<Device *>(this->controller[unWhichDevice - 1]);
	assert(dev);
	dev->update_pose(newPose);
}

void
Context::VsyncEvent(double vsyncTimeOffsetSeconds)
{}

void
Context::VendorSpecificEvent(uint32_t unWhichDevice,
                             vr::EVREventType eventType,
                             const vr::VREvent_Data_t &eventData,
                             double eventTimeOffset)
{}

bool
Context::IsExiting()
{
	return false;
}

void
Context::add_haptic_event(vr::VREvent_HapticVibration_t event)
{
	vr::VREvent_t e;
	e.eventType = vr::EVREventType::VREvent_Input_HapticVibration;
	e.trackedDeviceIndex = event.containerHandle - 1;
	vr::VREvent_Data_t d;
	d.hapticVibration = event;
	e.data = d;

	std::lock_guard lk(event_queue_mut);
	events.push_back({std::chrono::steady_clock::now(), e});
}

bool
Context::PollNextEvent(vr::VREvent_t *pEvent, uint32_t uncbVREvent)
{
	if (!events.empty()) {
		assert(sizeof(vr::VREvent_t) == uncbVREvent);
		Event e;
		{
			std::lock_guard lk(event_queue_mut);
			e = events.front();
			events.pop_front();
		}
		*pEvent = e.inner;
		using float_sec = std::chrono::duration<float>;
		float_sec event_age = std::chrono::steady_clock::now() - e.insert_time;
		pEvent->eventAgeSeconds = event_age.count();
		return true;
	}
	return false;
}

void
Context::GetRawTrackedDevicePoses(float fPredictedSecondsFromNow,
                                  vr::TrackedDevicePose_t *pTrackedDevicePoseArray,
                                  uint32_t unTrackedDevicePoseArrayCount)
{}

void
Context::RequestRestart(const char *pchLocalizedReason,
                        const char *pchExecutableToStart,
                        const char *pchArguments,
                        const char *pchWorkingDirectory)
{}

uint32_t
Context::GetFrameTimings(vr::Compositor_FrameTiming *pTiming, uint32_t nFrames)
{
	return 0;
}

void
Context::SetDisplayEyeToHead(uint32_t unWhichDevice,
                             const vr::HmdMatrix34_t &eyeToHeadLeft,
                             const vr::HmdMatrix34_t &eyeToHeadRight)
{}

void
Context::SetDisplayProjectionRaw(uint32_t unWhichDevice, const vr::HmdRect2_t &eyeLeft, const vr::HmdRect2_t &eyeRight)
{}

void
Context::SetRecommendedRenderTargetSize(uint32_t unWhichDevice, uint32_t nWidth, uint32_t nHeight)
{}

/***** IVRDriverInput methods *****/


vr::EVRInputError
Context::create_component_common(vr::PropertyContainerHandle_t container,
                                 const char *name,
                                 vr::VRInputComponentHandle_t *pHandle)
{
	*pHandle = vr::k_ulInvalidInputComponentHandle;
	Device *device = prop_container_to_device(container);
	if (!device) {
		return vr::VRInputError_InvalidHandle;
	}
	if (xrt_input *input = device->get_input_from_name(name); input) {
		CTX_DEBUG("creating component %s", name);
		vr::VRInputComponentHandle_t handle = handle_to_input.size() + 1;
		handle_to_input[handle] = input;
		*pHandle = handle;
	}
	return vr::VRInputError_None;
}

xrt_input *
Context::update_component_common(vr::VRInputComponentHandle_t handle,
                                 double offset,
                                 std::chrono::steady_clock::time_point now)
{
	xrt_input *input{nullptr};
	if (handle != vr::k_ulInvalidInputComponentHandle) {
		input = handle_to_input[handle];
		std::chrono::duration<double, std::chrono::seconds::period> offset_dur(offset);
		std::chrono::duration offset = (now + offset_dur).time_since_epoch();
		int64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(offset).count();
		input->active = true;
		input->timestamp = timestamp;
	}
	return input;
}

vr::EVRInputError
Context::CreateBooleanComponent(vr::PropertyContainerHandle_t ulContainer,
                                const char *pchName,
                                vr::VRInputComponentHandle_t *pHandle)
{
	return create_component_common(ulContainer, pchName, pHandle);
}

vr::EVRInputError
Context::UpdateBooleanComponent(vr::VRInputComponentHandle_t ulComponent, bool bNewValue, double fTimeOffset)
{
	xrt_input *input = update_component_common(ulComponent, fTimeOffset);
	if (input) {
		input->value.boolean = bNewValue;
	}
	return vr::VRInputError_None;
}

vr::EVRInputError
Context::CreateScalarComponent(vr::PropertyContainerHandle_t ulContainer,
                               const char *pchName,
                               vr::VRInputComponentHandle_t *pHandle,
                               vr::EVRScalarType eType,
                               vr::EVRScalarUnits eUnits)
{
	std::string_view name{pchName};
	// Lighthouse gives thumbsticks/trackpads as x/y components,
	// we need to combine them for Monado
	auto end = name.back();
	if (end == 'x' || end == 'y') {
		Device *device = prop_container_to_device(ulContainer);
		if (!device) {
			return vr::VRInputError_InvalidHandle;
		}
		bool x = end == 'x';
		name.remove_suffix(2);
		std::string n(name);
		xrt_input *input = device->get_input_from_name(n);
		if (!input) {
			return vr::VRInputError_None;
		}

		// Create the component mapping if it hasn't been created yet
		Vec2Components *components =
		    vec2_input_to_components.try_emplace(input, new Vec2Components).first->second.get();

		vr::VRInputComponentHandle_t new_handle = handle_to_input.size() + 1;
		if (x)
			components->x = new_handle;
		else
			components->y = new_handle;

		handle_to_input[new_handle] = input;
		*pHandle = new_handle;
		return vr::VRInputError_None;
	}
	return create_component_common(ulContainer, pchName, pHandle);
}

vr::EVRInputError
Context::UpdateScalarComponent(vr::VRInputComponentHandle_t ulComponent, float fNewValue, double fTimeOffset)
{
	xrt_input *input = update_component_common(ulComponent, fTimeOffset);
	if (input) {
		if (XRT_GET_INPUT_TYPE(input->name) == XRT_INPUT_TYPE_VEC2_MINUS_ONE_TO_ONE) {
			std::unique_ptr<Vec2Components> &components = vec2_input_to_components.at(input);
			if (components->x == ulComponent) {
				input->value.vec2.x = fNewValue;
			} else if (components->y == ulComponent) {
				input->value.vec2.y = fNewValue;
			} else {
				CTX_WARN(
				    "Attempted to update component with handle %lu"
				    " but it was neither the x nor y "
				    "component of its associated input",
				    ulComponent);
			}

		} else {
			input->value.vec1.x = fNewValue;
		}
	}
	return vr::VRInputError_None;
}

vr::EVRInputError
Context::CreateHapticComponent(vr::PropertyContainerHandle_t ulContainer,
                               const char *pchName,
                               vr::VRInputComponentHandle_t *pHandle)
{
	*pHandle = vr::k_ulInvalidInputComponentHandle;
	Device *d = prop_container_to_device(ulContainer);
	if (!d) {
		return vr::VRInputError_InvalidHandle;
	}

	// Assuming HMDs won't have haptics.
	// Maybe a wrong assumption.
	if (d == hmd) {
		CTX_WARN("Didn't expect HMD with haptics.");
		return vr::VRInputError_InvalidHandle;
	}

	auto *device = static_cast<ControllerDevice *>(d);
	vr::VRInputComponentHandle_t handle = handle_to_input.size() + 1;
	handle_to_input[handle] = nullptr;
	device->set_haptic_handle(handle);
	*pHandle = handle;

	return vr::VRInputError_None;
}

vr::EVRInputError
Context::CreateSkeletonComponent(vr::PropertyContainerHandle_t ulContainer,
                                 const char *pchName,
                                 const char *pchSkeletonPath,
                                 const char *pchBasePosePath,
                                 vr::EVRSkeletalTrackingLevel eSkeletalTrackingLevel,
                                 const vr::VRBoneTransform_t *pGripLimitTransforms,
                                 uint32_t unGripLimitTransformCount,
                                 vr::VRInputComponentHandle_t *pHandle)
{
	return vr::VRInputError_None;
}

vr::EVRInputError
Context::UpdateSkeletonComponent(vr::VRInputComponentHandle_t ulComponent,
                                 vr::EVRSkeletalMotionRange eMotionRange,
                                 const vr::VRBoneTransform_t *pTransforms,
                                 uint32_t unTransformCount)
{
	return vr::VRInputError_None;
}

/***** IVRProperties methods *****/

vr::ETrackedPropertyError
Context::ReadPropertyBatch(vr::PropertyContainerHandle_t ulContainerHandle,
                           vr::PropertyRead_t *pBatch,
                           uint32_t unBatchEntryCount)
{
	return vr::TrackedProp_Success;
}

vr::ETrackedPropertyError
Context::WritePropertyBatch(vr::PropertyContainerHandle_t ulContainerHandle,
                            vr::PropertyWrite_t *pBatch,
                            uint32_t unBatchEntryCount)
{
	Device *device = prop_container_to_device(ulContainerHandle);
	if (!device)
		return vr::TrackedProp_InvalidContainer;
	if (!pBatch)
		return vr::TrackedProp_InvalidOperation; // not verified vs steamvr
	device->handle_properties(pBatch, unBatchEntryCount);
	return vr::TrackedProp_Success;
}

const char *
Context::GetPropErrorNameFromEnum(vr::ETrackedPropertyError error)
{
	return nullptr;
}

Device *
Context::prop_container_to_device(vr::PropertyContainerHandle_t handle)
{
	switch (handle) {
	case 1: {
		return hmd;
		break;
	}
	case 2:
	case 3: {
		return controller[handle - 2];
		break;
	}
	default: {
		return nullptr;
	}
	}
}

vr::PropertyContainerHandle_t
Context::TrackedDeviceToPropertyContainer(vr::TrackedDeviceIndex_t nDevice)
{
	size_t container = nDevice + 1;
	if (nDevice == 0 && this->hmd)
		return container;
	if ((nDevice == 1 || nDevice == 2) && this->controller[nDevice - 1]) {
		return container;
	}

	return vr::k_ulInvalidPropertyContainer;
}

void
Context::Log(const char *pchLogMessage)
{
	CTX_TRACE("[lighthouse]: %s", pchLogMessage);
}
// NOLINTEND(bugprone-easily-swappable-parameters)


extern "C" int
steamvr_lh_get_devices(struct xrt_device **out_xdevs)
{
	u_logging_level level = debug_get_log_option_lh_log();
	// The driver likes to create a bunch of transient folder - lets make sure they're created where they normally
	// are.
	std::filesystem::current_path(STEAM_INSTALL_DIR + "/config/lighthouse");
	std::string steamvr = find_steamvr_install();
	if (steamvr.empty()) {
		U_LOG_IFL_E(level, "Could not find where SteamVR is installed!");
		return 0;
	}

	U_LOG_IFL_I(level, "Found SteamVR install: %s", steamvr.c_str());

	// TODO: support windows?
	auto driver_so = steamvr + "/drivers/lighthouse/bin/linux64/driver_lighthouse.so";

	void *lighthouse_lib = dlopen(driver_so.c_str(), RTLD_LAZY);
	if (!lighthouse_lib) {
		U_LOG_IFL_E(level, "Couldn't open lighthouse lib: %s", dlerror());
		return 0;
	}

	void *sym = dlsym(lighthouse_lib, "HmdDriverFactory");
	if (!sym) {
		U_LOG_IFL_E(level, "Couldn't find HmdDriverFactory in lighthouse lib: %s", dlerror());
		return 0;
	}
	using HmdDriverFactory_t = void *(*)(const char *, int *);
	auto factory = reinterpret_cast<HmdDriverFactory_t>(sym);

	vr::EVRInitError err = vr::VRInitError_None;
	auto *driver = static_cast<vr::IServerTrackedDeviceProvider *>(
	    factory(vr::IServerTrackedDeviceProvider_Version, (int *)&err));
	if (err != vr::VRInitError_None) {
		U_LOG_IFL_E(level, "Couldn't get tracked device driver: error %u", err);
		return 0;
	}

	std::shared_ptr ctx = Context::create(STEAM_INSTALL_DIR, steamvr, driver);

	err = driver->Init(ctx.get());
	if (err != vr::VRInitError_None) {
		U_LOG_IFL_E(level, "Lighthouse driver initialization failed: error %u", err);
		return 0;
	}

	U_LOG_IFL_I(level, "Lighthouse initialization complete, giving time to setup connected devices...");
	// RunFrame needs to be called to detect controllers
	using namespace std::chrono_literals;
	auto start_time = std::chrono::steady_clock::now();
	while (true) {
		driver->RunFrame();
		auto cur_time = std::chrono::steady_clock::now();
		if (cur_time - start_time >= 1s) {
			break;
		}
	}
	U_LOG_IFL_I(level, "Device search time complete.");

	int devices = 0;
	Device *devs[] = {ctx->hmd, ctx->controller[0], ctx->controller[1]};
	for (Device *dev : devs) {
		if (dev) {
			out_xdevs[devices++] = dev;
		}
	}
	return devices;
}
