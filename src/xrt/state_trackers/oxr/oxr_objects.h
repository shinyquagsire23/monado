// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Contains the instance struct that a lot of things hang from.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#pragma once

#include "xrt/xrt_device.h"
#include "xrt/xrt_compositor.h"
#include "xrt/xrt_vulkan_includes.h"
#include "xrt/xrt_openxr_includes.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @defgroup oxr OpenXR state tracker
 *
 * Client application facing code.
 *
 * @ingroup xrt
 */

/*!
 * @defgroup oxr_main OpenXR main code
 *
 * Gets called from @ref oxr_api functions and talks to devices and
 * @ref comp using @ref xrt_iface.
 *
 * @ingroup oxr
 * @{
 */

// For corruption and layer checking.
// clang-format off
#define OXR_XR_DEBUG_INSTANCE  (*(uint64_t *)"oxrinst\0")
#define OXR_XR_DEBUG_SESSION   (*(uint64_t *)"oxrsess\0")
#define OXR_XR_DEBUG_SPACE     (*(uint64_t *)"oxrspac\0")
#define OXR_XR_DEBUG_ACTION    (*(uint64_t *)"oxracti\0")
#define OXR_XR_DEBUG_SWAPCHAIN (*(uint64_t *)"oxrswap\0")
#define OXR_XR_DEBUG_ACTIONSET (*(uint64_t *)"oxraset\0")
#define OXR_XR_DEBUG_MESSENGER (*(uint64_t *)"oxrmess\0")
// clang-format on


/*
 *
 * Forward declare structs.
 *
 */

struct oxr_logger;
struct oxr_instance;
struct oxr_system;
struct oxr_session;
struct oxr_event;
struct oxr_swapchain;
struct oxr_space;
struct oxr_action_set;
struct oxr_action;
struct oxr_handle_base;

#define XRT_MAX_HANDLE_CHILDREN 256

struct time_state;

typedef XrResult (*oxr_handle_destroyer)(struct oxr_logger *log,
                                         struct oxr_handle_base *hb);

/*!
 * State of a handle base, to reduce likelyhood of going "boom" on
 * out-of-order destruction or other unsavory behavior.
 */
enum oxr_handle_state
{
	/*! State during/before oxr_handle_init, or after failure */
	OXR_HANDLE_STATE_UNINITIALIZED = 0,

	/*! State after successful oxr_handle_init */
	OXR_HANDLE_STATE_LIVE,

	/*! State after successful oxr_handle_destroy */
	OXR_HANDLE_STATE_DESTROYED,
};

/*
 *
 * oxr_handle_base.c
 *
 */

/*!
 * Destroy the handle's object, as well as all child handles recursively.
 *
 * This should be how all handle-associated objects are destroyed.
 */
XrResult
oxr_handle_destroy(struct oxr_logger *log, struct oxr_handle_base *hb);

/*!
 * Returns a human-readable label for a handle state.
 */
const char *
oxr_handle_state_to_string(enum oxr_handle_state state);

/*
 *
 * oxr_instance.c
 *
 */

/*!
 * To go back to a OpenXR object.
 */
XRT_MAYBE_UNUSED static inline XrInstance
oxr_instance_to_openxr(struct oxr_instance *inst)
{
	return (XrInstance)inst;
}

XrResult
oxr_instance_create(struct oxr_logger *log,
                    const XrInstanceCreateInfo *createInfo,
                    struct oxr_instance **out_inst);

XrResult
oxr_instance_get_properties(struct oxr_logger *log,
                            struct oxr_instance *inst,
                            XrInstanceProperties *instanceProperties);

#if XR_USE_TIMESPEC

XrResult
oxr_instance_convert_time_to_timespec(struct oxr_logger *log,
                                      struct oxr_instance *inst,
                                      XrTime time,
                                      struct timespec *timespecTime);
XrResult
oxr_instance_convert_timespec_to_time(struct oxr_logger *log,
                                      struct oxr_instance *inst,
                                      const struct timespec *timespecTime,
                                      XrTime *time);
#endif // XR_USE_TIMESPEC

/*
 *
 * oxr_session.c
 *
 */

/*!
 * To go back to a OpenXR object.
 */
XRT_MAYBE_UNUSED static inline XrSession
oxr_session_to_openxr(struct oxr_session *sess)
{
	return (XrSession)sess;
}

XrResult
oxr_session_create(struct oxr_logger *log,
                   struct oxr_system *sys,
                   XrStructureType *next,
                   struct oxr_session **out_session);

/*!
 * Internal destructor - not to be used directly!
 *
 * Use oxr_handle_destroy() to destroy a session.
 */
XrResult
oxr_session_destroy(struct oxr_logger *log, struct oxr_handle_base *hb);

XrResult
oxr_session_enumerate_formats(struct oxr_logger *log,
                              struct oxr_session *sess,
                              uint32_t formatCapacityInput,
                              uint32_t *formatCountOutput,
                              int64_t *formats);

XrResult
oxr_session_begin(struct oxr_logger *log,
                  struct oxr_session *sess,
                  const XrSessionBeginInfo *beginInfo);

XrResult
oxr_session_end(struct oxr_logger *log, struct oxr_session *sess);

/*!
 * Get the view space position at the given time in relation to the
 * local or stage space.
 */
XrResult
oxr_session_get_view_pose_at(struct oxr_logger *,
                             struct oxr_session *sess,
                             XrTime at_time,
                             struct xrt_pose *);

XrResult
oxr_session_views(struct oxr_logger *log,
                  struct oxr_session *sess,
                  const XrViewLocateInfo *viewLocateInfo,
                  XrViewState *viewState,
                  uint32_t viewCapacityInput,
                  uint32_t *viewCountOutput,
                  XrView *views);

XrResult
oxr_session_frame_wait(struct oxr_logger *log,
                       struct oxr_session *sess,
                       XrFrameState *frameState);

XrResult
oxr_session_frame_begin(struct oxr_logger *log, struct oxr_session *sess);

XrResult
oxr_session_frame_end(struct oxr_logger *log,
                      struct oxr_session *sess,
                      const XrFrameEndInfo *frameEndInfo);


/*
 *
 * oxr_space.c
 *
 */

/*!
 * To go back to a OpenXR object.
 */
XRT_MAYBE_UNUSED static inline XrSpace
oxr_space_to_openxr(struct oxr_space *spc)
{
	return (XrSpace)spc;
}

XrResult
oxr_space_action_create(struct oxr_logger *log,
                        struct oxr_action *act,
                        const XrActionSpaceCreateInfo *createInfo,
                        struct oxr_space **out_space);
XrResult
oxr_space_reference_create(struct oxr_logger *log,
                           struct oxr_session *sess,
                           const XrReferenceSpaceCreateInfo *createInfo,
                           struct oxr_space **out_space);

XrResult
oxr_space_locate(struct oxr_logger *log,
                 struct oxr_space *spc,
                 struct oxr_space *baseSpc,
                 XrTime time,
                 XrSpaceRelation *relation);

XrResult
oxr_space_ref_relation(struct oxr_logger *log,
                       struct oxr_session *sess,
                       XrReferenceSpaceType space,
                       XrReferenceSpaceType baseSpc,
                       XrTime time,
                       struct xrt_space_relation *out_relation);


/*
 *
 * oxr_swapchain.c
 *
 */

/*!
 * To go back to a OpenXR object.
 */
XRT_MAYBE_UNUSED static inline XrSwapchain
oxr_swapchain_to_openxr(struct oxr_swapchain *sc)
{
	return (XrSwapchain)sc;
}

XrResult
oxr_create_swapchain(struct oxr_logger *,
                     struct oxr_session *sess,
                     const XrSwapchainCreateInfo *,
                     struct oxr_swapchain **sc);


/*
 *

 * oxr_system.c
 *
 */

XrResult
oxr_system_select(struct oxr_logger *log,
                  struct oxr_system **systems,
                  uint32_t num_systems,
                  XrFormFactor form_factor,
                  struct oxr_system **out_selected);

XrResult
oxr_system_fill_in(struct oxr_logger *log,
                   struct oxr_instance *inst,
                   XrSystemId systemId,
                   struct oxr_system *sys,
                   struct xrt_device *dev);

XrResult
oxr_system_get_properties(struct oxr_logger *log,
                          struct oxr_system *sys,
                          XrSystemProperties *properties);

XrResult
oxr_system_enumerate_view_confs(
    struct oxr_logger *log,
    struct oxr_system *sys,
    uint32_t viewConfigurationTypeCapacityInput,
    uint32_t *viewConfigurationTypeCountOutput,
    XrViewConfigurationType *viewConfigurationTypes);

XrResult
oxr_system_enumerate_blend_modes(struct oxr_logger *log,
                                 struct oxr_system *sys,
                                 uint32_t environmentBlendModeCapacityInput,
                                 uint32_t *environmentBlendModeCountOutput,
                                 XrEnvironmentBlendMode *environmentBlendModes);

XrResult
oxr_system_get_view_conf_properties(
    struct oxr_logger *log,
    struct oxr_system *sys,
    XrViewConfigurationType viewConfigurationType,
    XrViewConfigurationProperties *configurationProperties);

XrResult
oxr_system_enumerate_view_conf_views(
    struct oxr_logger *log,
    struct oxr_system *sys,
    XrViewConfigurationType viewConfigurationType,
    uint32_t viewCapacityInput,
    uint32_t *viewCountOutput,
    XrViewConfigurationView *views);


/*
 *
 * oxr_event.cpp
 *
 */

XrResult
oxr_poll_event(struct oxr_logger *log,
               struct oxr_instance *inst,
               XrEventDataBuffer *eventData);

XrResult
oxr_event_push_XrEventDataSessionStateChanged(struct oxr_logger *log,
                                              struct oxr_session *sess,
                                              XrSessionState state,
                                              XrTime time);


/*
 *
 * OpenGL, located in various files.
 *
 */

#ifdef XR_USE_GRAPHICS_API_OPENGL
#ifdef XR_USE_PLATFORM_XLIB

XrResult
oxr_session_create_gl_xlib(struct oxr_logger *log,
                           struct oxr_system *sys,
                           XrGraphicsBindingOpenGLXlibKHR *next,
                           struct oxr_session **out_session);
#endif

XrResult
oxr_swapchain_gl_create(struct oxr_logger *,
                        struct oxr_session *sess,
                        const XrSwapchainCreateInfo *,
                        struct oxr_swapchain **out_swapchain);

#endif


/*
 *
 * Vulkan, located in various files.
 *
 */

#ifdef XR_USE_GRAPHICS_API_VULKAN

XrResult
oxr_vk_get_instance_exts(struct oxr_logger *log,
                         struct oxr_system *sys,
                         uint32_t namesCapacityInput,
                         uint32_t *namesCountOutput,
                         char *namesString);

XrResult
oxr_vk_get_device_exts(struct oxr_logger *log,
                       struct oxr_system *sys,
                       uint32_t namesCapacityInput,
                       uint32_t *namesCountOutput,
                       char *namesString);

XrResult
oxr_vk_get_requirements(struct oxr_logger *log,
                        struct oxr_system *sys,
                        XrGraphicsRequirementsVulkanKHR *graphicsRequirements);

XrResult
oxr_vk_get_physical_device(struct oxr_logger *log,
                           struct oxr_instance *inst,
                           struct oxr_system *sys,
                           VkInstance vkInstance,
                           PFN_vkGetInstanceProcAddr getProc,
                           VkPhysicalDevice *vkPhysicalDevice);

XrResult
oxr_session_create_vk(struct oxr_logger *log,
                      struct oxr_system *sys,
                      XrGraphicsBindingVulkanKHR *next,
                      struct oxr_session **out_session);

XrResult
oxr_swapchain_vk_create(struct oxr_logger *,
                        struct oxr_session *sess,
                        const XrSwapchainCreateInfo *,
                        struct oxr_swapchain **out_swapchain);

#endif


/*
 *
 * Structs
 *
 */


/*!
 * Used to hold diverse child handles and ensure orderly destruction.
 *
 * Each object referenced by an OpenXR handle should have one of these as its
 * first element.
 */
struct oxr_handle_base
{
	//! Magic (per-handle-type) value for debugging.
	uint64_t debug;

	/*!
	 * Pointer to this object's parent handle holder, if any.
	 */
	struct oxr_handle_base *parent;

	/*!
	 * Array of children, if any.
	 */
	struct oxr_handle_base *children[XRT_MAX_HANDLE_CHILDREN];

	/*!
	 * Current handle state.
	 */
	enum oxr_handle_state state;

	/*!
	 * Destroy the object this handle refers to.
	 */
	oxr_handle_destroyer destroy;
};

/*!
 * Single or multiple devices grouped together to form a system that sessions
 * can be created from. Might need to open devices in order to get all
 * properties from it, but shouldn't.
 *
 * @obj{XrSystemId}
 */
struct oxr_system
{
	struct oxr_instance *inst;
	struct xrt_device *device;
	XrSystemId systemId;

	XrFormFactor form_factor;
	XrViewConfigurationType view_config_type;
	XrViewConfigurationView views[2];
	uint32_t num_blend_modes;
	XrEnvironmentBlendMode blend_modes[3];
};

/*!
 * Main object that ties everything together.
 *
 * @obj{XrInstance}
 */
struct oxr_instance
{
	//! Common structure for things referred to by OpenXR handles.
	struct oxr_handle_base handle;

	struct xrt_prober *prober;

	// Enabled extensions
	bool headless;
	bool opengl_enable;
	bool vulkan_enable;

	// Hardcoded single system.
	struct oxr_system system;

	struct time_state *timekeeping;

	// Event queue.
	struct oxr_event *last_event;
	struct oxr_event *next_event;
};

/*!
 * Object that client program interact with.
 *
 * @obj{XrSession}
 */
struct oxr_session
{
	//! Common structure for things referred to by OpenXR handles.
	struct oxr_handle_base handle;
	struct oxr_system *sys;
	struct xrt_compositor *compositor;

	XrSessionState state;
	bool frame_started;

	/*!
	 * IPD, to be expanded to a proper 3D relation.
	 */
	float ipd_meters;

	float static_prediction_s;

	/*!
	 * To pipe swapchain creation to right code.
	 */
	XrResult (*create_swapchain)(struct oxr_logger *,
	                             struct oxr_session *sess,
	                             const XrSwapchainCreateInfo *,
	                             struct oxr_swapchain **);
};

/*!
 * Can be one of 3 references or a space that are bound to actions.
 *
 * @obj{XrSpace}
 */
struct oxr_space
{
	//! Common structure for things referred to by OpenXR handles.
	struct oxr_handle_base handle;

	//! Onwer of this space.
	struct oxr_session *sess;

	//! Pose that was given during creation.
	struct xrt_pose pose;

	//! What kind of reference space is this, if any.
	XrReferenceSpaceType type;

	//! Is this a reference space?
	bool is_reference;
};

/*!
 * A set of images used for rendering.
 *
 * @obj{XrSwapchain}
 */
struct oxr_swapchain
{
	//! Common structure for things referred to by OpenXR handles.
	struct oxr_handle_base handle;

	//! Onwer of this swapchain.
	struct oxr_session *sess;

	//! Compositor swapchain.
	struct xrt_swapchain *swapchain;

	//! Actual state tracked! :D
	int acquired_index;

	XrResult (*destroy)(struct oxr_logger *, struct oxr_swapchain *);

	XrResult (*enumerate_images)(struct oxr_logger *,
	                             struct oxr_swapchain *,
	                             uint32_t,
	                             XrSwapchainImageBaseHeader *);

	XrResult (*acquire_image)(struct oxr_logger *,
	                          struct oxr_swapchain *,
	                          const XrSwapchainImageAcquireInfo *,
	                          uint32_t *);

	XrResult (*wait_image)(struct oxr_logger *,
	                       struct oxr_swapchain *,
	                       const XrSwapchainImageWaitInfo *);

	XrResult (*release_image)(struct oxr_logger *,
	                          struct oxr_swapchain *,
	                          const XrSwapchainImageReleaseInfo *);
};

/*!
 * A group of actions.
 *
 * @obj{XrActionSet}
 */
struct oxr_action_set
{
	//! Common structure for things referred to by OpenXR handles.
	struct oxr_handle_base handle;

	//! Onwer of this messenger.
	struct oxr_session *sess;
};

/*!
 * A single action.
 *
 * @obj{XrAction}
 */
struct oxr_action
{
	//! Common structure for things referred to by OpenXR handles.
	struct oxr_handle_base handle;

	//! Onwer of this messenger.
	struct oxr_action_set *act_set;
};

/*!
 * Debug object created by the client program.
 *
 * @obj{XrDebugUtilsMessengerEXT}
 */
struct oxr_debug_messenger
{
	//! Common structure for things referred to by OpenXR handles.
	struct oxr_handle_base handle;

	//! Onwer of this messenger.
	struct oxr_instance *inst;
};

/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
