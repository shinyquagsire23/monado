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
#include "xrt/xrt_tracking.h"
#include "xrt/xrt_compositor.h"
#include "xrt/xrt_vulkan_includes.h"
#include "xrt/xrt_openxr_includes.h"
#include "util/u_hashset.h"
#include "util/u_hashmap.h"

#include "oxr_extension_support.h"

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
 * @brief Cast a pointer to an OpenXR handle in such a way as to avoid warnings.
 *
 * Avoids -Wpointer-to-int-cast by first casting to the same size int, then
 * promoting to the 64-bit int, then casting to the handle type. That's a lot of
 * no-ops on 64-bit, but a widening conversion on 32-bit.
 *
 * @ingroup oxr
 */
#define XRT_CAST_PTR_TO_OXR_HANDLE(HANDLE_TYPE, PTR)                           \
	((HANDLE_TYPE)(uint64_t)(uintptr_t)(PTR))

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
#define OXR_XR_DEBUG_PATH      (*(uint64_t *)"oxrpath\0")
#define OXR_XR_DEBUG_ACTION    (*(uint64_t *)"oxracti\0")
#define OXR_XR_DEBUG_SWAPCHAIN (*(uint64_t *)"oxrswap\0")
#define OXR_XR_DEBUG_ACTIONSET (*(uint64_t *)"oxraset\0")
#define OXR_XR_DEBUG_MESSENGER (*(uint64_t *)"oxrmess\0")
#define OXR_XR_DEBUG_SOURCESET (*(uint64_t *)"oxrsrcs\0")
#define OXR_XR_DEBUG_SOURCE    (*(uint64_t *)"oxrsrc_\0")
// clang-format on


/*
 *
 * Forward declare structs.
 *
 */

struct xrt_instance;
struct oxr_logger;
struct oxr_instance;
struct oxr_system;
struct oxr_session;
struct oxr_event;
struct oxr_swapchain;
struct oxr_space;
struct oxr_action_set;
struct oxr_action;
struct oxr_debug_messenger;
struct oxr_handle_base;
struct oxr_sub_paths;
struct oxr_source;
struct oxr_source_set;
struct oxr_source_input;
struct oxr_source_output;
struct oxr_binding;
struct oxr_interaction_profile;

#define XRT_MAX_HANDLE_CHILDREN 256

struct time_state;

typedef XrResult (*oxr_handle_destroyer)(struct oxr_logger *log,
                                         struct oxr_handle_base *hb);

/*!
 * State of a handle base, to reduce likelihood of going "boom" on
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

/*!
 * Sub action paths.
 */
enum oxr_sub_action_path
{
	OXR_SUB_ACTION_PATH_USER,
	OXR_SUB_ACTION_PATH_HEAD,
	OXR_SUB_ACTION_PATH_LEFT,
	OXR_SUB_ACTION_PATH_RIGHT,
	OXR_SUB_ACTION_PATH_GAMEPAD,
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
static inline XrInstance
oxr_instance_to_openxr(struct oxr_instance *inst)
{
	return XRT_CAST_PTR_TO_OXR_HANDLE(XrInstance, inst);
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
 * oxr_path.c
 *
 */

void *
oxr_path_get_attached(struct oxr_logger *log,
                      struct oxr_instance *inst,
                      XrPath path);

/*!
 * Get the path for the given string if it exists, or create it if it does not.
 */
XrResult
oxr_path_get_or_create(struct oxr_logger *log,
                       struct oxr_instance *inst,
                       const char *str,
                       size_t length,
                       XrPath *out_path);

/*!
 * Only get the path for the given string if it exists.
 */
XrResult
oxr_path_only_get(struct oxr_logger *log,
                  struct oxr_instance *inst,
                  const char *str,
                  size_t length,
                  XrPath *out_path);

/*!
 * Get a pointer and length of the internal string.
 *
 * The pointer has the same life time as the instance. The length is the number
 * of valid characters, not including the null termination character (but a
 * extra null byte is always reserved at the end so can strings can be given
 * to functions expecting null terminated strings).
 */
XrResult
oxr_path_get_string(struct oxr_logger *log,
                    struct oxr_instance *inst,
                    XrPath path,
                    const char **out_str,
                    size_t *out_length);

/*!
 * Destroy all paths that the instance has created.
 */
void
oxr_path_destroy_all(struct oxr_logger *log, struct oxr_instance *inst);


/*
 *
 * oxr_input.c
 *
 */

/*!
 * Helper function to classify sub_paths.
 */
void
oxr_classify_sub_action_paths(struct oxr_logger *log,
                              struct oxr_instance *inst,
                              uint32_t num_subaction_paths,
                              const XrPath *subaction_paths,
                              struct oxr_sub_paths *sub_paths);

/*!
 * Find the pose input for the set of sub_paths
 */
XrResult
oxr_source_get_pose_input(struct oxr_logger *log,
                          struct oxr_session *sess,
                          uint32_t key,
                          const struct oxr_sub_paths *sub_paths,
                          struct oxr_source_input **out_input);

/*!
 * To go back to a OpenXR object.
 */
static inline XrActionSet
oxr_action_set_to_openxr(struct oxr_action_set *act_set)
{
	return XRT_CAST_PTR_TO_OXR_HANDLE(XrActionSet, act_set);
}

XrResult
oxr_action_set_create(struct oxr_logger *log,
                      struct oxr_instance *inst,
                      const XrActionSetCreateInfo *createInfo,
                      struct oxr_action_set **out_act_set);

/*!
 * To go back to a OpenXR object.
 */
static inline XrAction
oxr_action_to_openxr(struct oxr_action *act)
{
	return XRT_CAST_PTR_TO_OXR_HANDLE(XrAction, act);
}

XrResult
oxr_action_create(struct oxr_logger *log,
                  struct oxr_action_set *act_set,
                  const XrActionCreateInfo *createInfo,
                  struct oxr_action **out_act);

XrResult
oxr_session_attach_action_sets(struct oxr_logger *log,
                               struct oxr_session *sess,
                               const XrSessionActionSetsAttachInfo *bindInfo);

XrResult
oxr_action_sync_data(struct oxr_logger *log,
                     struct oxr_session *sess,
                     uint32_t countActionSets,
                     const XrActiveActionSet *actionSets);

XrResult
oxr_action_get_boolean(struct oxr_logger *log,
                       struct oxr_session *sess,
                       uint64_t key,
                       struct oxr_sub_paths sub_paths,
                       XrActionStateBoolean *data);

XrResult
oxr_action_get_vector1f(struct oxr_logger *log,
                        struct oxr_session *sess,
                        uint64_t key,
                        struct oxr_sub_paths sub_paths,
                        XrActionStateFloat *data);


XrResult
oxr_action_get_vector2f(struct oxr_logger *log,
                        struct oxr_session *sess,
                        uint64_t key,
                        struct oxr_sub_paths sub_paths,
                        XrActionStateVector2f *data);

XrResult
oxr_action_get_pose(struct oxr_logger *log,
                    struct oxr_session *sess,
                    uint64_t key,
                    struct oxr_sub_paths sub_paths,
                    XrActionStatePose *data);

XrResult
oxr_action_apply_haptic_feedback(struct oxr_logger *log,
                                 struct oxr_session *sess,
                                 uint64_t key,
                                 struct oxr_sub_paths sub_paths,
                                 const XrHapticBaseHeader *hapticEvent);

XrResult
oxr_action_stop_haptic_feedback(struct oxr_logger *log,
                                struct oxr_session *sess,
                                uint64_t key,
                                struct oxr_sub_paths sub_paths);


/*
 *
 * oxr_binding.c
 *
 */

/*!
 * Find the best matching profile for the given @ref xrt_device.
 *
 * @param      log   Logger.
 * @param      inst  Instance.
 * @param      xdev  Can be null.
 * @param[out] out_p Returned interaction profile.
 */
void
oxr_find_profile_for_device(struct oxr_logger *log,
                            struct oxr_instance *inst,
                            struct xrt_device *xdev,
                            struct oxr_interaction_profile **out_p);

/*!
 * Free all memory allocated by the binding system.
 */
void
oxr_binding_destroy_all(struct oxr_logger *log, struct oxr_instance *inst);

/*!
 * Find all bindings that is the given action key is bound to.
 */
void
oxr_binding_find_bindings_from_key(struct oxr_logger *log,
                                   struct oxr_interaction_profile *profile,
                                   uint32_t key,
                                   struct oxr_binding *bindings[32],
                                   size_t *num_bindings);

XrResult
oxr_action_suggest_interaction_profile_bindings(
    struct oxr_logger *log,
    struct oxr_instance *inst,
    const XrInteractionProfileSuggestedBinding *suggestedBindings);

XrResult
oxr_action_get_current_interaction_profile(
    struct oxr_logger *log,
    struct oxr_session *sess,
    XrPath topLevelUserPath,
    XrInteractionProfileState *interactionProfile);

XrResult
oxr_action_get_input_source_localized_name(
    struct oxr_logger *log,
    struct oxr_session *sess,
    const XrInputSourceLocalizedNameGetInfo *getInfo,
    uint32_t bufferCapacityInput,
    uint32_t *bufferCountOutput,
    char *buffer);

XrResult
oxr_action_enumerate_bound_sources(struct oxr_logger *log,
                                   struct oxr_session *sess,
                                   uint64_t key,
                                   uint32_t sourceCapacityInput,
                                   uint32_t *sourceCountOutput,
                                   XrPath *sources);


/*
 *
 * oxr_session.c
 *
 */

/*!
 * To go back to a OpenXR object.
 */
static inline XrSession
oxr_session_to_openxr(struct oxr_session *sess)
{
	return XRT_CAST_PTR_TO_OXR_HANDLE(XrSession, sess);
}

XrResult
oxr_session_create(struct oxr_logger *log,
                   struct oxr_system *sys,
                   const XrSessionCreateInfo *createInfo,
                   struct oxr_session **out_session);

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

XrResult
oxr_session_request_exit(struct oxr_logger *log, struct oxr_session *sess);

void
oxr_session_poll(struct oxr_session *sess);

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
static inline XrSpace
oxr_space_to_openxr(struct oxr_space *spc)
{
	return XRT_CAST_PTR_TO_OXR_HANDLE(XrSpace, spc);
}

XrResult
oxr_space_action_create(struct oxr_logger *log,
                        struct oxr_session *sess,
                        uint64_t key,
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
                 XrSpaceLocation *location);

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
static inline XrSwapchain
oxr_swapchain_to_openxr(struct oxr_swapchain *sc)
{
	return XRT_CAST_PTR_TO_OXR_HANDLE(XrSwapchain, sc);
}

XrResult
oxr_create_swapchain(struct oxr_logger *,
                     struct oxr_session *sess,
                     const XrSwapchainCreateInfo *,
                     struct oxr_swapchain **out_swapchain);


/*
 *
 * oxr_messenger.c
 *
 */

/*!
 * To go back to a OpenXR object.
 */
static inline XrDebugUtilsMessengerEXT
oxr_messenger_to_openxr(struct oxr_debug_messenger *mssngr)
{
	return XRT_CAST_PTR_TO_OXR_HANDLE(XrDebugUtilsMessengerEXT, mssngr);
}

XrResult
oxr_create_messenger(struct oxr_logger *,
                     struct oxr_instance *inst,
                     const XrDebugUtilsMessengerCreateInfoEXT *,
                     struct oxr_debug_messenger **out_mssngr);
XrResult
oxr_destroy_messenger(struct oxr_logger *log,
                      struct oxr_debug_messenger *mssngr);


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
                   struct xrt_device **xdevs,
                   size_t num_xdevs);

XrResult
oxr_system_verify_id(struct oxr_logger *log,
                     const struct oxr_instance *inst,
                     XrSystemId systemId);

XrResult
oxr_system_get_by_id(struct oxr_logger *log,
                     struct oxr_instance *inst,
                     XrSystemId systemId,
                     struct oxr_system **system);

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
                                 XrViewConfigurationType viewConfigurationType,
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
 * oxr_xdev.c
 *
 */

void
oxr_xdev_destroy(struct xrt_device **xdev_ptr);

void
oxr_xdev_update(struct xrt_device *xdev);

/*!
 * Return true if it finds an input of that name on this device.
 */
bool
oxr_xdev_find_input(struct xrt_device *xdev,
                    enum xrt_input_name name,
                    struct xrt_input **out_input);

/*!
 * Return true if it finds an output of that name on this device.
 */
bool
oxr_xdev_find_output(struct xrt_device *xdev,
                     enum xrt_output_name name,
                     struct xrt_output **out_output);

/*!
 * Returns the pose of the named input from the device, if the pose isn't valid
 * uses the device offset instead.
 */
void
oxr_xdev_get_pose_at(struct oxr_logger *log,
                     struct oxr_instance *inst,
                     struct xrt_device *xdev,
                     enum xrt_input_name name,
                     XrTime at_time,
                     uint64_t *out_pose_timestamp_ns,
                     struct xrt_pose *out_pose);

/*!
 * Returns the relation of the named input from the device, always ensures
 * that position and orientation is valid by using the device offset.
 */
void
oxr_xdev_get_relation_at(struct oxr_logger *log,
                         struct oxr_instance *inst,
                         struct xrt_device *xdev,
                         enum xrt_input_name name,
                         XrTime at_time,
                         uint64_t *out_relation_timestamp_ns,
                         struct xrt_space_relation *out_relation);


/*
 *
 * OpenGL, located in various files.
 *
 */

#ifdef XR_USE_GRAPHICS_API_OPENGL
#ifdef XR_USE_PLATFORM_XLIB

XrResult
oxr_session_populate_gl_xlib(struct oxr_logger *log,
                             struct oxr_system *sys,
                             XrGraphicsBindingOpenGLXlibKHR const *next,
                             struct oxr_session *sess);
#endif // XR_USE_PLATFORM_XLIB

XrResult
oxr_swapchain_gl_create(struct oxr_logger *,
                        struct oxr_session *sess,
                        const XrSwapchainCreateInfo *,
                        struct oxr_swapchain **out_swapchain);

#endif // XR_USE_GRAPHICS_API_OPENGL


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
oxr_session_populate_vk(struct oxr_logger *log,
                        struct oxr_system *sys,
                        XrGraphicsBindingVulkanKHR const *next,
                        struct oxr_session *sess);

XrResult
oxr_swapchain_vk_create(struct oxr_logger *,
                        struct oxr_session *sess,
                        const XrSwapchainCreateInfo *,
                        struct oxr_swapchain **out_swapchain);

#endif


/*
 *
 * EGL, located in various files.
 *
 */

#ifdef XR_USE_PLATFORM_EGL

XrResult
oxr_session_populate_egl(struct oxr_logger *log,
                         struct oxr_system *sys,
                         XrGraphicsBindingEGLMND const *next,
                         struct oxr_session *sess);

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

	union {
		struct
		{
			struct xrt_device *head;
			struct xrt_device *left;
			struct xrt_device *right;
		};
		struct xrt_device *xdevs[16];
	};
	size_t num_xdevs;

	XrSystemId systemId;

	XrFormFactor form_factor;
	XrViewConfigurationType view_config_type;
	XrViewConfigurationView views[2];
	uint32_t num_blend_modes;
	XrEnvironmentBlendMode blend_modes[3];
};

#define MAKE_EXT_STATUS(mixed_case, all_caps) bool mixed_case;
/*!
 * Structure tracking which extensions are enabled for a given instance.
 *
 * Names are systematic: the extension name with the XR_ prefix removed.
 */
struct oxr_extension_status
{
	OXR_EXTENSION_SUPPORT_GENERATE(MAKE_EXT_STATUS)
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

	/* ---- HACK ---- */
	void *hack;
	/* ---- HACK ---- */

	struct xrt_instance *xinst;

	//! Enabled extensions
	struct oxr_extension_status extensions;

	// Hardcoded single system.
	struct oxr_system system;

	struct time_state *timekeeping;

	//! Path store, for looking up paths.
	struct u_hashset *path_store;

	// Event queue.
	struct oxr_event *last_event;
	struct oxr_event *next_event;

	struct oxr_interaction_profile **profiles;
	size_t num_profiles;

	struct oxr_session *sessions;

	struct
	{
		XrPath user;
		XrPath head;
		XrPath left;
		XrPath right;
		XrPath gamepad;

		XrPath khr_simple_controller;
		XrPath google_daydream_controller;
		XrPath htc_vive_controller;
		XrPath htc_vive_pro;
		XrPath microsoft_motion_controller;
		XrPath microsoft_xbox_controller;
		XrPath oculus_go_controller;
		XrPath oculus_touch_controller;
		XrPath valve_index_controller;
		XrPath mnd_ball_on_stick_controller;
	} path_cache;

	//! Debug messengers
	struct oxr_debug_messenger *messengers[XRT_MAX_HANDLE_CHILDREN];

	bool lifecycle_verbose;
	bool debug_views;
	bool debug_spaces;
	bool debug_bindings;
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

	struct oxr_session *next;

	XrSessionState state;
	bool frame_started;
	bool exiting;

	struct u_hashmap_int *act_sets;
	struct u_hashmap_int *sources;

	//! List of created source sets.
	struct oxr_source_set *src_set_list;

	//! Has xrAttachSessionActionSets been called?
	bool actionsAttached;

	/*!
	 * Currently bound interaction profile.
	 * @{
	 */
	XrPath left;
	XrPath right;
	XrPath head;
	XrPath gamepad;
	/*!
	 * @}
	 */

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
 * Returns XR_SUCCESS or XR_SESSION_LOSS_PENDING as appropriate.
 */
static inline XrResult
oxr_session_success_result(struct oxr_session *session)
{
	switch (session->state) {
	case XR_SESSION_STATE_LOSS_PENDING: return XR_SESSION_LOSS_PENDING;
	default: return XR_SUCCESS;
	}
}

/*!
 * Returns XR_SUCCESS, XR_SESSION_LOSS_PENDING, or XR_SESSION_NOT_FOCUSED, as
 * appropriate.
 */
static inline XrResult
oxr_session_success_focused_result(struct oxr_session *session)
{
	switch (session->state) {
	case XR_SESSION_STATE_LOSS_PENDING: return XR_SESSION_LOSS_PENDING;
	case XR_SESSION_STATE_FOCUSED: return XR_SUCCESS;
	default: return XR_SESSION_NOT_FOCUSED;
	}
}

/*!
 * A single interaction profile.
 */
struct oxr_interaction_profile
{
	XrPath path;
	struct oxr_binding *bindings;
	size_t num_bindings;
};

/*!
 * Interaction profile binding state.
 */
struct oxr_binding
{
	XrPath *paths;
	size_t num_paths;

	enum oxr_sub_action_path sub_path;

	uint32_t *keys;
	size_t num_keys;

	enum xrt_input_name *inputs;
	size_t num_inputs;

	enum xrt_output_name *outputs;
	size_t num_outputs;
};

/*!
 * To carry around a sementic selection of sub action paths.
 */
struct oxr_sub_paths
{
	bool any;
	bool user;
	bool head;
	bool left;
	bool right;
	bool gamepad;
};

/*!
 * Session input source.
 *
 * @see oxr_action_set
 */
struct oxr_source_set
{
	//! Common structure for things referred to by OpenXR handles.
	struct oxr_handle_base handle;

	//! Owning session.
	struct oxr_session *sess;

	//! Which sub-action paths are requested on the latest sync.
	struct oxr_sub_paths requested_sub_paths;

	//! Next source set on this session.
	struct oxr_source_set *next;
};

/*!
 * The state of a action input source.
 *
 * @see oxr_source
 */
struct oxr_source_state
{
	union {
		struct
		{
			float x;
		} vec1;

		struct
		{
			float x;
			float y;
		} vec2;

		bool boolean;
	};

	bool active;

	// Was this changed.
	bool changed;

	//! When was this last changed.
	XrTime timestamp;
};

/*!
 * A input source pair of a @ref xrt_input and a @ref xrt_device.
 *
 * @see xrt_device
 * @see xrt_input
 */
struct oxr_source_input
{
	struct xrt_device *xdev;
	struct xrt_input *input;
};

/*!
 * A output source pair of a @ref xrt_output_name and a @ref xrt_device.
 *
 * @see xrt_device
 * @see xrt_output_name
 */
struct oxr_source_output
{
	struct xrt_device *xdev;
	enum xrt_output_name name;
};

/*!
 * A set of inputs for a single sub action path.
 *
 * @see oxr_source
 */
struct oxr_source_cache
{
	struct oxr_source_state current;

	size_t num_inputs;
	struct oxr_source_input *inputs;

	int64_t stop_output_time;
	size_t num_outputs;
	struct oxr_source_output *outputs;
};

/*!
 * Session input source.
 *
 * @see oxr_action
 */
struct oxr_source
{
	//! Common structure for things referred to by OpenXR handles.
	struct oxr_handle_base handle;

	//! Type the action this source was created from is.
	XrActionType action_type;

	struct oxr_source_state any_state;

	struct oxr_source_cache user;
	struct oxr_source_cache head;
	struct oxr_source_cache left;
	struct oxr_source_cache right;
	struct oxr_source_cache gamepad;
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

	//! Action key from which action this space was created from.
	uint32_t act_key;

	//! Is this a reference space?
	bool is_reference;

	//! Which sub action path is this?
	struct oxr_sub_paths sub_paths;
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
	int released_index;

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
	struct oxr_instance *inst;

	//! Application supplied name of this action.
	char name[XR_MAX_ACTION_SET_NAME_SIZE];

	//! Has this action set been attached.
	bool attached;

	//! Unique key for the session hashmap.
	uint32_t key;
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

	//! Application supplied name of this action.
	char name[XR_MAX_ACTION_NAME_SIZE];

	//! Unique key for the session hashmap.
	uint32_t key;

	//! Type this action was created with.
	XrActionType action_type;

	//! Which sub action paths that this action was created with.
	struct oxr_sub_paths sub_paths;
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

	//! Severities to submit to this messenger
	XrDebugUtilsMessageSeverityFlagsEXT message_severities;

	//! Types to submit to this messenger
	XrDebugUtilsMessageTypeFlagsEXT message_types;

	//! Callback function
	PFN_xrDebugUtilsMessengerCallbackEXT user_callback;

	//! Opaque user data
	void *XR_MAY_ALIAS user_data;
};

/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
