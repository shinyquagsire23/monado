// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds shipped binding data.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#include "oxr_objects.h"


struct binding_template
{
	enum oxr_subaction_path subaction_path;

	const char *localized_name;

	const char *paths[8];
	enum xrt_input_name input;
	enum xrt_output_name output;
};

struct profile_template
{
	enum xrt_device_name name;

	const char *path;

	const char *localized_name;

	struct binding_template *bindings;
	size_t num_bindings;
};


/*
 *
 * Shipped bindings.
 *
 */

#define MAKE_INPUT(COMPONENT, NAME, SUFFIX, INPUT)                                                                     \
	{                                                                                                              \
	    .subaction_path = OXR_SUB_ACTION_PATH_LEFT,                                                                \
	    .localized_name = NAME,                                                                                    \
	    .paths =                                                                                                   \
	        {                                                                                                      \
	            "/user/hand/left/input/" #COMPONENT "/" #SUFFIX,                                                   \
	            "/user/hand/left/input/" #COMPONENT,                                                               \
	            NULL,                                                                                              \
	        },                                                                                                     \
	    .input = INPUT,                                                                                            \
	},                                                                                                             \
	    {                                                                                                          \
	        .subaction_path = OXR_SUB_ACTION_PATH_RIGHT,                                                           \
	        .localized_name = NAME,                                                                                \
	        .paths =                                                                                               \
	            {                                                                                                  \
	                "/user/hand/right/input/" #COMPONENT "/" #SUFFIX,                                              \
	                "/user/hand/right/input/" #COMPONENT,                                                          \
	                NULL,                                                                                          \
	            },                                                                                                 \
	        .input = INPUT,                                                                                        \
	    },

// creates an input that can not be "downgraded" to the top level path.
// e.g. don't bind ../trackpad/click, ../trackpad/touch with just ../trackpad
#define MAKE_INPUT_SUFFIX_ONLY(COMPONENT, NAME, SUFFIX, INPUT)                                                         \
	{                                                                                                              \
	    .subaction_path = OXR_SUB_ACTION_PATH_LEFT,                                                                \
	    .localized_name = NAME,                                                                                    \
	    .paths =                                                                                                   \
	        {                                                                                                      \
	            "/user/hand/left/input/" #COMPONENT "/" #SUFFIX,                                                   \
	            NULL,                                                                                              \
	        },                                                                                                     \
	    .input = INPUT,                                                                                            \
	},                                                                                                             \
	    {                                                                                                          \
	        .subaction_path = OXR_SUB_ACTION_PATH_RIGHT,                                                           \
	        .localized_name = NAME,                                                                                \
	        .paths =                                                                                               \
	            {                                                                                                  \
	                "/user/hand/right/input/" #COMPONENT "/" #SUFFIX,                                              \
	                NULL,                                                                                          \
	            },                                                                                                 \
	        .input = INPUT,                                                                                        \
	    },

// creates an input with a top level path and /x and /y sub paths
#define MAKE_INPUT_VEC2F(COMPONENT, NAME, INPUT)                                                                       \
	{                                                                                                              \
	    .subaction_path = OXR_SUB_ACTION_PATH_LEFT,                                                                \
	    .localized_name = NAME,                                                                                    \
	    .paths =                                                                                                   \
	        {                                                                                                      \
	            "/user/hand/left/input/" #COMPONENT,                                                               \
	            "/user/hand/left/input/" #COMPONENT "/x",                                                          \
	            "/user/hand/left/input/" #COMPONENT "/y",                                                          \
	            NULL,                                                                                              \
	        },                                                                                                     \
	    .input = INPUT,                                                                                            \
	},                                                                                                             \
	    {                                                                                                          \
	        .subaction_path = OXR_SUB_ACTION_PATH_RIGHT,                                                           \
	        .localized_name = NAME,                                                                                \
	        .paths =                                                                                               \
	            {                                                                                                  \
	                "/user/hand/right/input/" #COMPONENT,                                                          \
	                "/user/hand/right/input/" #COMPONENT "/x",                                                     \
	                "/user/hand/right/input/" #COMPONENT "/y",                                                     \
	                NULL,                                                                                          \
	            },                                                                                                 \
	        .input = INPUT,                                                                                        \
	    },

#define MAKE_OUTPUT(COMPONENT, NAME, OUTPUT)                                                                           \
	{                                                                                                              \
	    .subaction_path = OXR_SUB_ACTION_PATH_LEFT,                                                                \
	    .localized_name = NAME,                                                                                    \
	    .paths =                                                                                                   \
	        {                                                                                                      \
	            "/user/hand/left/output/" #COMPONENT,                                                              \
	            NULL,                                                                                              \
	        },                                                                                                     \
	    .output = OUTPUT,                                                                                          \
	},                                                                                                             \
	    {                                                                                                          \
	        .subaction_path = OXR_SUB_ACTION_PATH_RIGHT,                                                           \
	        .localized_name = NAME,                                                                                \
	        .paths =                                                                                               \
	            {                                                                                                  \
	                "/user/hand/right/output/" #COMPONENT,                                                         \
	                NULL,                                                                                          \
	            },                                                                                                 \
	        .output = OUTPUT,                                                                                      \
	    },


/*
 *
 * KHR Simple Controller
 *
 */

static struct binding_template khr_simple_controller_bindings[10] = {
    // clang-format off
    MAKE_INPUT(select, "Select", click, XRT_INPUT_SIMPLE_SELECT_CLICK)
    MAKE_INPUT(menu, "Menu", click, XRT_INPUT_SIMPLE_MENU_CLICK)
    MAKE_INPUT(grip, "Grip", pose, XRT_INPUT_SIMPLE_GRIP_POSE)
    MAKE_INPUT(aim, "Aim", pose, XRT_INPUT_SIMPLE_AIM_POSE)

    MAKE_OUTPUT(haptic, "Haptic", XRT_OUTPUT_NAME_SIMPLE_VIBRATION)
    // clang-format on
};


/*
 *
 * Monado ball on a stick controller
 *
 */

static struct binding_template mndx_ball_on_a_stick_controller_bindings[26] = {
    // clang-format off
    MAKE_INPUT(system, "PS™ Logo", click, XRT_INPUT_PSMV_PS_CLICK)
    MAKE_INPUT(menu, "Move™ Logo", click, XRT_INPUT_PSMV_MOVE_CLICK)
    MAKE_INPUT(start, "Start/Options", click, XRT_INPUT_PSMV_START_CLICK)
    MAKE_INPUT(select, "Select", click, XRT_INPUT_PSMV_SELECT_CLICK)
    MAKE_INPUT(square_mndx, "Square™", click, XRT_INPUT_PSMV_SQUARE_CLICK)
    MAKE_INPUT(cross_mndx, "Cross™", click, XRT_INPUT_PSMV_CROSS_CLICK)
    MAKE_INPUT(circle_mndx, "Circle™", click, XRT_INPUT_PSMV_CIRCLE_CLICK)
    MAKE_INPUT(triangle_mndx, "Trinangle™", click, XRT_INPUT_PSMV_TRIANGLE_CLICK)
    MAKE_INPUT(trigger, "Trigger", value, XRT_INPUT_PSMV_TRIGGER_VALUE)
    MAKE_INPUT(grip, "Grip", pose, XRT_INPUT_PSMV_GRIP_POSE)
    MAKE_INPUT(ball_mndx, "Ball", pose, XRT_INPUT_PSMV_BALL_CENTER_POSE)
    MAKE_INPUT(aim, "Aim", pose, XRT_INPUT_PSMV_AIM_POSE)

    MAKE_OUTPUT(haptic, "Haptic", XRT_OUTPUT_NAME_PSMV_RUMBLE_VIBRATION)
    // clang-format on
};


/*
 *
 * Valve Index controller
 *
 */

static struct binding_template valve_index_controller_bindings[44] = {
    // clang-format off
	MAKE_INPUT(system, "System", click, XRT_INPUT_INDEX_SYSTEM_CLICK)
	MAKE_INPUT(system, "System Touch", touch, XRT_INPUT_INDEX_SYSTEM_TOUCH)
	MAKE_INPUT(a, "A", click, XRT_INPUT_INDEX_A_CLICK)
	MAKE_INPUT(a, "A Touch", touch, XRT_INPUT_INDEX_A_TOUCH)
	MAKE_INPUT(b, "B", click, XRT_INPUT_INDEX_B_CLICK)
	MAKE_INPUT(b, "B Touch", touch, XRT_INPUT_INDEX_B_TOUCH)
	MAKE_INPUT(squeeze, "Side-Squeeze", value, XRT_INPUT_INDEX_SQUEEZE_VALUE)
	MAKE_INPUT(squeeze, "Side-Squeeze Force", force, XRT_INPUT_INDEX_SQUEEZE_FORCE)
	MAKE_INPUT(trigger, "Trigger Click", click, XRT_INPUT_INDEX_TRIGGER_CLICK)
	MAKE_INPUT(trigger, "Trigger", value, XRT_INPUT_INDEX_TRIGGER_VALUE)
	MAKE_INPUT(trigger, "Trigger Touch", touch, XRT_INPUT_INDEX_TRIGGER_TOUCH)
	MAKE_INPUT_VEC2F(thumbstick, "Thumbstick", XRT_INPUT_INDEX_THUMBSTICK)
	MAKE_INPUT_SUFFIX_ONLY(thumbstick, "Thumbstick Click", click, XRT_INPUT_INDEX_THUMBSTICK_CLICK)
	MAKE_INPUT_SUFFIX_ONLY(thumbstick, "Thumbstick Touch", touch, XRT_INPUT_INDEX_THUMBSTICK_TOUCH)
	MAKE_INPUT_VEC2F(trackpad, "Trackpad", XRT_INPUT_INDEX_TRACKPAD)
	MAKE_INPUT_SUFFIX_ONLY(trackpad, "Trackpad Force", force, XRT_INPUT_INDEX_TRACKPAD_FORCE)
	MAKE_INPUT_SUFFIX_ONLY(trackpad, "Trackpad Touch", touch, XRT_INPUT_INDEX_TRACKPAD_TOUCH)
	MAKE_INPUT(grip, "Grip", pose, XRT_INPUT_INDEX_GRIP_POSE)
	MAKE_INPUT(aim, "Aim", pose, XRT_INPUT_INDEX_AIM_POSE)

	MAKE_OUTPUT(haptic, "Haptic", XRT_OUTPUT_NAME_INDEX_HAPTIC)
    // clang-format on
};


/*
 *
 * HTC Vive controller
 *
 */

static struct binding_template htc_vive_controller_bindings[24] = {
    // clang-format off
	MAKE_INPUT(system, "System", click, XRT_INPUT_VIVE_SYSTEM_CLICK)
	MAKE_INPUT(squeeze, "Side-Squeeze", click, XRT_INPUT_VIVE_SQUEEZE_CLICK)
	MAKE_INPUT(menu, "Menu", click, XRT_INPUT_VIVE_MENU_CLICK)
	MAKE_INPUT(trigger, "Trigger Click", click, XRT_INPUT_VIVE_TRIGGER_CLICK)
	MAKE_INPUT(trigger, "Trigger", value, XRT_INPUT_VIVE_TRIGGER_VALUE)
	MAKE_INPUT_VEC2F(trackpad, "Trackpad", XRT_INPUT_VIVE_TRACKPAD)
	MAKE_INPUT_SUFFIX_ONLY(trackpad, "Trackpad Click", click, XRT_INPUT_VIVE_TRACKPAD_CLICK)
	MAKE_INPUT_SUFFIX_ONLY(trackpad, "Trackpad Touch", touch, XRT_INPUT_VIVE_TRACKPAD_TOUCH)
	MAKE_INPUT(grip, "Grip", pose, XRT_INPUT_VIVE_GRIP_POSE)
	MAKE_INPUT(aim, "Aim", pose, XRT_INPUT_VIVE_AIM_POSE)

	MAKE_OUTPUT(haptic, "Haptic", XRT_OUTPUT_NAME_VIVE_HAPTIC)
    // clang-format on
};


/*
 *
 * Profiles
 *
 */

static struct profile_template profiles[4] = {
    {
        .name = XRT_DEVICE_SIMPLE_CONTROLLER,
        .path = "/interaction_profiles/khr/simple_controller",
        .localized_name = "Simple Controller",
        .bindings = khr_simple_controller_bindings,
        .num_bindings = ARRAY_SIZE(khr_simple_controller_bindings),
    },
#if 0
    {
        .path = "/interaction_profiles/google/daydream_controller",
        .localized_name = "Daydream Controller",
        .bindings = google_daydream_controller_bindings,
        .num_bindings = ARRAY_SIZE(google_daydream_controller_bindings),
    },
#endif
    {
        .name = XRT_DEVICE_PSMV,
        .path = "/interaction_profiles/mndx/ball_on_a_stick_controller",
        .localized_name = "PS Move",
        .bindings = mndx_ball_on_a_stick_controller_bindings,
        .num_bindings = ARRAY_SIZE(mndx_ball_on_a_stick_controller_bindings),
    },
    {
        .name = XRT_DEVICE_INDEX_CONTROLLER,
        .path = "/interaction_profiles/valve/index_controller",
        .localized_name = "Index Controller",
        .bindings = valve_index_controller_bindings,
        .num_bindings = ARRAY_SIZE(valve_index_controller_bindings),
    },
    {
        .name = XRT_DEVICE_VIVE_WAND,
        .path = "/interaction_profiles/htc/vive_controller",
        .localized_name = "Vive Wand",
        .bindings = htc_vive_controller_bindings,
        .num_bindings = ARRAY_SIZE(htc_vive_controller_bindings),
    },
};
