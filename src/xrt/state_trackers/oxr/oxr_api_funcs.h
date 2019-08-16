// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Header defining all API functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_api
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @defgroup oxr_api OpenXR entrypoints
 *
 * Gets called from the client application, does most verification and routes
 * calls into @ref oxr_main functions.
 *
 * @ingroup oxr
 * @{
 */


/*
 *
 * oxr_api_negotiate.c
 *
 */

//! OpenXR API function @ep{xrGetInstanceProcAddr}
XrResult
oxr_xrGetInstanceProcAddr(XrInstance instance,
                          const char* name,
                          PFN_xrVoidFunction* function);

//! OpenXR API function @ep{xrEnumerateApiLayerProperties}
XrResult
oxr_xrEnumerateApiLayerProperties(uint32_t propertyCapacityInput,
                                  uint32_t* propertyCountOutput,
                                  XrApiLayerProperties* properties);


/*
 *
 * oxr_api_instance.c
 *
 */

//! OpenXR API function @ep{xrEnumerateInstanceExtensionProperties}
XrResult
oxr_xrEnumerateInstanceExtensionProperties(const char* layerName,
                                           uint32_t propertyCapacityInput,
                                           uint32_t* propertyCountOutput,
                                           XrExtensionProperties* properties);

//! OpenXR API function @ep{xrCreateInstance}
XrResult
oxr_xrCreateInstance(const XrInstanceCreateInfo* createInfo,
                     XrInstance* instance);

//! OpenXR API function @ep{xrDestroyInstance}
XrResult
oxr_xrDestroyInstance(XrInstance instance);

//! OpenXR API function @ep{xrGetInstanceProperties}
XrResult
oxr_xrGetInstanceProperties(XrInstance instance,
                            XrInstanceProperties* instanceProperties);

//! OpenXR API function @ep{xrPollEvent}
XrResult
oxr_xrPollEvent(XrInstance instance, XrEventDataBuffer* eventData);

//! OpenXR API function @ep{xrResultToString}
XrResult
oxr_xrResultToString(XrInstance instance,
                     XrResult value,
                     char buffer[XR_MAX_RESULT_STRING_SIZE]);

//! OpenXR API function @ep{xrStructureTypeToString}
XrResult
oxr_xrStructureTypeToString(XrInstance instance,
                            XrStructureType value,
                            char buffer[XR_MAX_STRUCTURE_NAME_SIZE]);

//! OpenXR API function @ep{xrStringToPath}
XrResult
oxr_xrStringToPath(XrInstance instance, const char* pathString, XrPath* path);

//! OpenXR API function @ep{xrPathToString}
XrResult
oxr_xrPathToString(XrInstance instance,
                   XrPath path,
                   uint32_t bufferCapacityInput,
                   uint32_t* bufferCountOutput,
                   char* buffer);

//! OpenXR API function @ep{xrConvertTimespecTimeToTimeKHR}
XrResult
oxr_xrConvertTimespecTimeToTimeKHR(XrInstance instance,
                                   const struct timespec* timespecTime,
                                   XrTime* time);

//! OpenXR API function @ep{xrConvertTimeToTimespecTimeKHR}
XrResult
oxr_xrConvertTimeToTimespecTimeKHR(XrInstance instance,
                                   XrTime time,
                                   struct timespec* timespecTime);

/*
 *
 * oxr_api_system.c
 *
 */

//! OpenXR API function @ep{xrGetSystem}
XrResult
oxr_xrGetSystem(XrInstance instance,
                const XrSystemGetInfo* getInfo,
                XrSystemId* systemId);

//! OpenXR API function @ep{xrGetSystemProperties}
XrResult
oxr_xrGetSystemProperties(XrInstance instance,
                          XrSystemId systemId,
                          XrSystemProperties* properties);

//! OpenXR API function @ep{xrEnumerateViewConfigurations}
XrResult
oxr_xrEnumerateViewConfigurations(
    XrInstance instance,
    XrSystemId systemId,
    uint32_t viewConfigurationTypeCapacityInput,
    uint32_t* viewConfigurationTypeCountOutput,
    XrViewConfigurationType* viewConfigurationTypes);

//! OpenXR API function @ep{xrGetViewConfigurationProperties}
XrResult
oxr_xrGetViewConfigurationProperties(
    XrInstance instance,
    XrSystemId systemId,
    XrViewConfigurationType viewConfigurationType,
    XrViewConfigurationProperties* configurationProperties);

//! OpenXR API function @ep{xrEnumerateViewConfigurationViews}
XrResult
oxr_xrEnumerateViewConfigurationViews(
    XrInstance instance,
    XrSystemId systemId,
    XrViewConfigurationType viewConfigurationType,
    uint32_t viewCapacityInput,
    uint32_t* viewCountOutput,
    XrViewConfigurationView* views);

//! OpenXR API function @ep{xrEnumerateEnvironmentBlendModes}
XrResult
oxr_xrEnumerateEnvironmentBlendModes(
    XrInstance instance,
    XrSystemId systemId,
    XrViewConfigurationType viewConfigurationType,
    uint32_t environmentBlendModeCapacityInput,
    uint32_t* environmentBlendModeCountOutput,
    XrEnvironmentBlendMode* environmentBlendModes);

#ifdef XR_USE_GRAPHICS_API_OPENGL
//! OpenXR API function @ep{xrGetOpenGLGraphicsRequirementsKHR}
XrResult
oxr_xrGetOpenGLGraphicsRequirementsKHR(
    XrInstance instance,
    XrSystemId systemId,
    XrGraphicsRequirementsOpenGLKHR* graphicsRequirements);
#endif

#ifdef XR_USE_GRAPHICS_API_VULKAN
//! OpenXR API function @ep{xrGetVulkanInstanceExtensionsKHR}
XrResult
oxr_xrGetVulkanInstanceExtensionsKHR(XrInstance instance,
                                     XrSystemId systemId,
                                     uint32_t namesCapacityInput,
                                     uint32_t* namesCountOutput,
                                     char* namesString);

//! OpenXR API function @ep{xrGetVulkanDeviceExtensionsKHR}
XrResult
oxr_xrGetVulkanDeviceExtensionsKHR(XrInstance instance,
                                   XrSystemId systemId,
                                   uint32_t namesCapacityInput,
                                   uint32_t* namesCountOutput,
                                   char* namesString);

//! OpenXR API function @ep{xrGetVulkanGraphicsDeviceKHR}
XrResult
oxr_xrGetVulkanGraphicsDeviceKHR(XrInstance instance,
                                 XrSystemId systemId,
                                 VkInstance vkInstance,
                                 VkPhysicalDevice* vkPhysicalDevice);

//! OpenXR API function @ep{xrGetVulkanGraphicsRequirementsKHR}
XrResult
oxr_xrGetVulkanGraphicsRequirementsKHR(
    XrInstance instance,
    XrSystemId systemId,
    XrGraphicsRequirementsVulkanKHR* graphicsRequirements);
#endif


/*
 *
 * oxr_api_session.c
 *
 */

//! OpenXR API function @ep{xrCreateSession}
XrResult
oxr_xrCreateSession(XrInstance instance,
                    const XrSessionCreateInfo* createInfo,
                    XrSession* session);

//! OpenXR API function @ep{xrDestroySession}
XrResult
oxr_xrDestroySession(XrSession session);

//! OpenXR API function @ep{xrBeginSession}
XrResult
oxr_xrBeginSession(XrSession session, const XrSessionBeginInfo* beginInfo);

//! OpenXR API function @ep{xrEndSession}
XrResult
oxr_xrEndSession(XrSession session);

//! OpenXR API function @ep{xrWaitFrame}
XrResult
oxr_xrWaitFrame(XrSession session,
                const XrFrameWaitInfo* frameWaitInfo,
                XrFrameState* frameState);

//! OpenXR API function @ep{xrBeginFrame}
XrResult
oxr_xrBeginFrame(XrSession session, const XrFrameBeginInfo* frameBeginInfo);

//! OpenXR API function @ep{xrEndFrame}
XrResult
oxr_xrEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo);

//! OpenXR API function @ep{xrRequestExitSession}
XrResult
oxr_xrRequestExitSession(XrSession session);

//! OpenXR API function @ep{xrLocateViews}
XrResult
oxr_xrLocateViews(XrSession session,
                  const XrViewLocateInfo* viewLocateInfo,
                  XrViewState* viewState,
                  uint32_t viewCapacityInput,
                  uint32_t* viewCountOutput,
                  XrView* views);

#ifdef XR_KHR_visibility_mask
//! OpenXR API function @ep{xrGetVisibilityMaskKHR}
XrResult
oxr_xrGetVisibilityMaskKHR(XrSession session,
                           XrViewConfigurationType viewConfigurationType,
                           uint32_t viewIndex,
                           XrVisibilityMaskTypeKHR visibilityMaskType,
                           XrVisibilityMaskKHR* visibilityMask);
#endif

#ifdef XR_EXT_performance_settings
//! OpenXR API function @ep{xrPerfSettingsSetPerformanceLevelEXT}
XrResult
oxr_xrPerfSettingsSetPerformanceLevelEXT(XrSession session,
                                         XrPerfSettingsDomainEXT domain,
                                         XrPerfSettingsLevelEXT level);
#endif

#ifdef XR_EXT_thermal_query
//! OpenXR API function @ep{xrThermalGetTemperatureTrendEXT}
XrResult
oxr_xrThermalGetTemperatureTrendEXT(
    XrSession session,
    XrPerfSettingsDomainEXT domain,
    XrPerfSettingsNotificationLevelEXT* notificationLevel,
    float* tempHeadroom,
    float* tempSlope);
#endif


/*
 *
 * oxr_api_space.c
 *
 */

//! OpenXR API function @ep{xrEnumerateReferenceSpaces}
XrResult
oxr_xrEnumerateReferenceSpaces(XrSession session,
                               uint32_t spaceCapacityInput,
                               uint32_t* spaceCountOutput,
                               XrReferenceSpaceType* spaces);

//! OpenXR API function @ep{xrGetReferenceSpaceBoundsRect}
XrResult
oxr_xrGetReferenceSpaceBoundsRect(XrSession session,
                                  XrReferenceSpaceType referenceSpaceType,
                                  XrExtent2Df* bounds);

//! OpenXR API function @ep{xrCreateReferenceSpace}
XrResult
oxr_xrCreateReferenceSpace(XrSession session,
                           const XrReferenceSpaceCreateInfo* createInfo,
                           XrSpace* space);

//! OpenXR API function @ep{xrLocateSpace}
XrResult
oxr_xrLocateSpace(XrSpace space,
                  XrSpace baseSpace,
                  XrTime time,
                  XrSpaceLocation* location);

//! OpenXR API function @ep{xrDestroySpace}
XrResult
oxr_xrDestroySpace(XrSpace space);


/*
 *
 * oxr_api_swapchain.c
 *
 */

//! OpenXR API function @ep{xrEnumerateSwapchainFormats}
XrResult
oxr_xrEnumerateSwapchainFormats(XrSession session,
                                uint32_t formatCapacityInput,
                                uint32_t* formatCountOutput,
                                int64_t* formats);

//! OpenXR API function @ep{xrCreateSwapchain}
XrResult
oxr_xrCreateSwapchain(XrSession session,
                      const XrSwapchainCreateInfo* createInfo,
                      XrSwapchain* swapchain);

//! OpenXR API function @ep{xrDestroySwapchain}
XrResult
oxr_xrDestroySwapchain(XrSwapchain swapchain);

//! OpenXR API function @ep{xrEnumerateSwapchainImages}
XrResult
oxr_xrEnumerateSwapchainImages(XrSwapchain swapchain,
                               uint32_t imageCapacityInput,
                               uint32_t* imageCountOutput,
                               XrSwapchainImageBaseHeader* images);

//! OpenXR API function @ep{xrAcquireSwapchainImage}
XrResult
oxr_xrAcquireSwapchainImage(XrSwapchain swapchain,
                            const XrSwapchainImageAcquireInfo* acquireInfo,
                            uint32_t* index);

//! OpenXR API function @ep{xrWaitSwapchainImage}
XrResult
oxr_xrWaitSwapchainImage(XrSwapchain swapchain,
                         const XrSwapchainImageWaitInfo* waitInfo);

//! OpenXR API function @ep{xrReleaseSwapchainImage}
XrResult
oxr_xrReleaseSwapchainImage(XrSwapchain swapchain,
                            const XrSwapchainImageReleaseInfo* releaseInfo);


/*
 *
 * oxr_api_debug.c
 *
 */

#ifdef XR_EXT_debug_utils

//! OpenXR API function @ep{xrSetDebugUtilsObjectNameEXT}
XrResult
oxr_xrSetDebugUtilsObjectNameEXT(XrInstance instance,
                                 const XrDebugUtilsObjectNameInfoEXT* nameInfo);

//! OpenXR API function @ep{xrCreateDebugUtilsMessengerEXT}
XrResult
oxr_xrCreateDebugUtilsMessengerEXT(
    XrInstance instance,
    const XrDebugUtilsMessengerCreateInfoEXT* createInfo,
    XrDebugUtilsMessengerEXT* messenger);

//! OpenXR API function @ep{xrDestroyDebugUtilsMessengerEXT}
XrResult
oxr_xrDestroyDebugUtilsMessengerEXT(XrDebugUtilsMessengerEXT messenger);

//! OpenXR API function @ep{xrSubmitDebugUtilsMessageEXT}
XrResult
oxr_xrSubmitDebugUtilsMessageEXT(
    XrInstance instance,
    XrDebugUtilsMessageSeverityFlagsEXT messageSeverity,
    XrDebugUtilsMessageTypeFlagsEXT messageTypes,
    const XrDebugUtilsMessengerCallbackDataEXT* callbackData);

//! OpenXR API function @ep{xrSessionBeginDebugUtilsLabelRegionEXT}
XrResult
oxr_xrSessionBeginDebugUtilsLabelRegionEXT(
    XrSession session, const XrDebugUtilsLabelEXT* labelInfo);

//! OpenXR API function @ep{xrSessionEndDebugUtilsLabelRegionEXT}
XrResult
oxr_xrSessionEndDebugUtilsLabelRegionEXT(XrSession session);

//! OpenXR API function @ep{xrSessionInsertDebugUtilsLabelEXT}
XrResult
oxr_xrSessionInsertDebugUtilsLabelEXT(XrSession session,
                                      const XrDebugUtilsLabelEXT* labelInfo);
#endif


/*
 *
 * oxr_api_action.c
 *
 */

//! OpenXR API function @ep{xrCreateActionSpace}
XrResult
oxr_xrCreateActionSpace(XrSession session,
                        const XrActionSpaceCreateInfo* createInfo,
                        XrSpace* space);

//! OpenXR API function @ep{xrCreateActionSet}
XrResult
oxr_xrCreateActionSet(XrInstance instance,
                      const XrActionSetCreateInfo* createInfo,
                      XrActionSet* actionSet);

//! OpenXR API function @ep{xrDestroyActionSet}
XrResult
oxr_xrDestroyActionSet(XrActionSet actionSet);

//! OpenXR API function @ep{xrCreateAction}
XrResult
oxr_xrCreateAction(XrActionSet actionSet,
                   const XrActionCreateInfo* createInfo,
                   XrAction* action);

//! OpenXR API function @ep{xrDestroyAction}
XrResult
oxr_xrDestroyAction(XrAction action);

//! OpenXR API function @ep{xrSuggestInteractionProfileBindings}
XrResult
oxr_xrSuggestInteractionProfileBindings(
    XrInstance instance,
    const XrInteractionProfileSuggestedBinding* suggestedBindings);

//! OpenXR API function @ep{xrAttachSessionActionSets}
XrResult
oxr_xrAttachSessionActionSets(XrSession session,
                              const XrSessionActionSetsAttachInfo* bindInfo);

//! OpenXR API function @ep{xrGetCurrentInteractionProfile}
XrResult
oxr_xrGetCurrentInteractionProfile(
    XrSession session,
    XrPath topLevelUserPath,
    XrInteractionProfileState* interactionProfile);

//! OpenXR API function @ep{xrGetActionStateBoolean}
XrResult
oxr_xrGetActionStateBoolean(XrSession session,
                            const XrActionStateGetInfo* getInfo,
                            XrActionStateBoolean* data);

//! OpenXR API function @ep{xrGetActionStateFloat}
XrResult
oxr_xrGetActionStateFloat(XrSession session,
                          const XrActionStateGetInfo* getInfo,
                          XrActionStateFloat* data);

//! OpenXR API function @ep{xrGetActionStateVector2f}
XrResult
oxr_xrGetActionStateVector2f(XrSession session,
                             const XrActionStateGetInfo* getInfo,
                             XrActionStateVector2f* data);

//! OpenXR API function @ep{xrGetActionStatePose}
XrResult
oxr_xrGetActionStatePose(XrSession session,
                         const XrActionStateGetInfo* getInfo,
                         XrActionStatePose* data);

//! OpenXR API function @ep{xrSyncActions}
XrResult
oxr_xrSyncActions(XrSession session, const XrActionsSyncInfo* syncInfo);

//! OpenXR API function @ep{xrEnumerateBoundSourcesForAction}
XrResult
oxr_xrEnumerateBoundSourcesForAction(
    XrSession session,
    const XrBoundSourcesForActionEnumerateInfo* enumerateInfo,
    uint32_t sourceCapacityInput,
    uint32_t* sourceCountOutput,
    XrPath* sources);

//! OpenXR API function @ep{xrGetInputSourceLocalizedName}
XrResult
oxr_xrGetInputSourceLocalizedName(
    XrSession session,
    const XrInputSourceLocalizedNameGetInfo* getInfo,
    uint32_t bufferCapacityInput,
    uint32_t* bufferCountOutput,
    char* buffer);

//! OpenXR API function @ep{xrApplyHapticFeedback}
XrResult
oxr_xrApplyHapticFeedback(XrSession session,
                          const XrHapticActionInfo* hapticActionInfo,
                          const XrHapticBaseHeader* hapticEvent);

//! OpenXR API function @ep{xrStopHapticFeedback}
XrResult
oxr_xrStopHapticFeedback(XrSession session,
                         const XrHapticActionInfo* hapticActionInfo);


/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
