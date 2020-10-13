// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds shipped binding data.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

// #include "xrt/xrt_compiler.h"

#include "oxr_objects.h"


struct binding_template
{
	enum oxr_sub_action_path sub_path;

	const char *localized_name;

	const char *paths[8];
	enum xrt_input_name inputs[8];
	enum xrt_output_name outputs[8];
};

struct profile_template
{
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

/*
 *
 *
 *
 * KHR Simple Controller
 *
 *
 *
 */

static struct binding_template khr_simple_controller_bindings[10] = {
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "Select",
        .paths =
            {
                "/user/hand/left/input/select/click",
                "/user/hand/left/input/select",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_SIMPLE_SELECT_CLICK,
                XRT_INPUT_PSMV_TRIGGER_VALUE,
                XRT_INPUT_HYDRA_TRIGGER_VALUE,
                XRT_INPUT_DAYDREAM_TOUCHPAD_CLICK,
                XRT_INPUT_INDEX_TRIGGER_VALUE,
                XRT_INPUT_VIVE_TRIGGER_VALUE,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "Menu",
        .paths =
            {
                "/user/hand/left/input/menu/click",
                "/user/hand/left/input/menu",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_SIMPLE_MENU_CLICK,
                XRT_INPUT_PSMV_MOVE_CLICK,
                XRT_INPUT_HYDRA_MIDDLE_CLICK,
                XRT_INPUT_DAYDREAM_BAR_CLICK,
                XRT_INPUT_INDEX_B_CLICK,
                XRT_INPUT_VIVE_MENU_CLICK,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "Grip",
        .paths =
            {
                "/user/hand/left/input/grip/pose",
                "/user/hand/left/input/grip",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_SIMPLE_GRIP_POSE,
                XRT_INPUT_PSMV_GRIP_POSE,
                XRT_INPUT_HYDRA_POSE,
                XRT_INPUT_DAYDREAM_POSE,
                XRT_INPUT_INDEX_GRIP_POSE,
                XRT_INPUT_VIVE_GRIP_POSE,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "Aim",
        .paths =
            {
                "/user/hand/left/input/aim/pose",
                "/user/hand/left/input/aim",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_SIMPLE_AIM_POSE,
                XRT_INPUT_PSMV_AIM_POSE,
                XRT_INPUT_HYDRA_POSE,
                XRT_INPUT_DAYDREAM_POSE,
                XRT_INPUT_INDEX_AIM_POSE,
                XRT_INPUT_VIVE_AIM_POSE,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "Haptic",
        .paths =
            {
                "/user/hand/left/output/haptic",
                NULL,
            },
        .outputs =
            {
                XRT_OUTPUT_NAME_SIMPLE_VIBRATION,
                XRT_OUTPUT_NAME_PSMV_RUMBLE_VIBRATION,
                XRT_OUTPUT_NAME_INDEX_HAPTIC,
                XRT_OUTPUT_NAME_VIVE_HAPTIC,
                (enum xrt_output_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "Select",
        .paths =
            {
                "/user/hand/right/input/select/click",
                "/user/hand/right/input/select",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_SIMPLE_SELECT_CLICK,
                XRT_INPUT_PSMV_TRIGGER_VALUE,
                XRT_INPUT_HYDRA_TRIGGER_VALUE,
                XRT_INPUT_DAYDREAM_TOUCHPAD_CLICK,
                XRT_INPUT_INDEX_TRIGGER_VALUE,
                XRT_INPUT_VIVE_TRIGGER_VALUE,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "Menu",
        .paths =
            {
                "/user/hand/right/input/menu/click",
                "/user/hand/right/input/menu",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_SIMPLE_MENU_CLICK,
                XRT_INPUT_PSMV_MOVE_CLICK,
                XRT_INPUT_HYDRA_MIDDLE_CLICK,
                XRT_INPUT_DAYDREAM_BAR_CLICK,
                XRT_INPUT_INDEX_B_CLICK,
                XRT_INPUT_VIVE_MENU_CLICK,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "Grip",
        .paths =
            {
                "/user/hand/right/input/grip/pose",
                "/user/hand/right/input/grip",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_SIMPLE_GRIP_POSE,
                XRT_INPUT_PSMV_GRIP_POSE,
                XRT_INPUT_HYDRA_POSE,
                XRT_INPUT_DAYDREAM_POSE,
                XRT_INPUT_INDEX_GRIP_POSE,
                XRT_INPUT_VIVE_GRIP_POSE,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "Aim",
        .paths =
            {
                "/user/hand/right/input/aim/pose",
                "/user/hand/right/input/aim",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_SIMPLE_AIM_POSE,
                XRT_INPUT_PSMV_AIM_POSE,
                XRT_INPUT_HYDRA_POSE,
                XRT_INPUT_DAYDREAM_POSE,
                XRT_INPUT_INDEX_AIM_POSE,
                XRT_INPUT_VIVE_AIM_POSE,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "Haptic",
        .paths =
            {
                "/user/hand/right/output/haptic",
                NULL,
            },
        .outputs =
            {
                XRT_OUTPUT_NAME_SIMPLE_VIBRATION,
                XRT_OUTPUT_NAME_PSMV_RUMBLE_VIBRATION,
                XRT_OUTPUT_NAME_INDEX_HAPTIC,
                XRT_OUTPUT_NAME_VIVE_HAPTIC,
                (enum xrt_output_name)0,
            },
    },
};


/*
 *
 *
 *
 * Google Daydream Controller
 *
 *
 *
 */

static struct binding_template google_daydream_controller_bindings[12] = {
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "Select",
        .paths =
            {
                "/user/hand/left/input/select/click",
                "/user/hand/left/input/select",
                NULL,
            },
        .inputs =
            {
#if 0
                XRT_INPUT_PSMV_TRIGGER_VALUE,
                XRT_INPUT_HYDRA_TRIGGER_VALUE,
#endif
                (enum xrt_input_name)0,
            },
    },
    {
        //! @todo Flag that this is a trackpad
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "Trackpad",
        .paths =
            {
                "/user/hand/left/input/trackpad",
                "/user/hand/left/input/trackpad/x",
                "/user/hand/left/input/trackpad/y",
                NULL,
            },
        .inputs =
            {
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "Trackpad Click",
        .paths =
            {
                "/user/hand/left/input/trackpad/click",
                "/user/hand/left/input/trackpad",
                NULL,
            },
        .inputs =
            {
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "Trackpad Touch",
        .paths =
            {
                "/user/hand/left/input/trackpad/touch",
                NULL,
            },
        .inputs =
            {
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "Grip",
        .paths =
            {
                "/user/hand/left/input/grip/pose",
                "/user/hand/left/input/grip",
                NULL,
            },
        .inputs =
            {
#if 0
                XRT_INPUT_PSMV_GRIP_POSE,
                XRT_INPUT_HYDRA_POSE,
#endif
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "Aim",
        .paths =
            {
                "/user/hand/left/input/aim/pose",
                "/user/hand/left/input/aim",
                NULL,
            },
        .inputs =
            {
#if 0
                XRT_INPUT_PSMV_AIM_POSE,
                XRT_INPUT_HYDRA_POSE,
#endif
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "Select",
        .paths =
            {
                "/user/hand/right/input/select/click",
                "/user/hand/right/input/select",
                NULL,
            },
        .inputs =
            {
#if 0
                XRT_INPUT_PSMV_TRIGGER_VALUE,
                XRT_INPUT_HYDRA_TRIGGER_VALUE,
#endif
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "Trackpad",
        .paths =
            {
                "/user/hand/right/input/trackpad",
                "/user/hand/right/input/trackpad/x",
                "/user/hand/right/input/trackpad/y",
                NULL,
            },
        .inputs =
            {
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "Trackpad Click",
        .paths =
            {
                "/user/hand/right/input/trackpad/click",
                "/user/hand/right/input/trackpad",
                NULL,
            },
        .inputs =
            {
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "Trackpad Touch",
        .paths =
            {
                "/user/hand/right/input/trackpad/touch",
                NULL,
            },
        .inputs =
            {
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "Grip",
        .paths =
            {
                "/user/hand/right/input/grip/pose",
                "/user/hand/right/input/grip",
                NULL,
            },
        .inputs =
            {
#if 0
                XRT_INPUT_PSMV_GRIP_POSE,
                XRT_INPUT_HYDRA_POSE,
#endif
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "Aim",
        .paths =
            {
                "/user/hand/right/input/aim/pose",
                "/user/hand/right/input/aim",
                NULL,
            },
        .inputs =
            {
#if 0
                XRT_INPUT_PSMV_AIM_POSE,
                XRT_INPUT_HYDRA_POSE,
#endif
                (enum xrt_input_name)0,
            },
    },
};


/*
 *
 *
 *
 * Monado ball on a stick controller
 *
 *
 *
 */

static struct binding_template mndx_ball_on_a_stick_controller_bindings[26] = {
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "PS™ Logo",
        .paths =
            {
                "/user/hand/left/input/system/click",
                "/user/hand/left/input/system",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_PS_CLICK,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "Move™ Logo",
        .paths =
            {
                "/user/hand/left/input/menu/click",
                "/user/hand/left/input/menu",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_MOVE_CLICK,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "Start/Options",
        .paths =
            {
                "/user/hand/left/input/start/click",
                "/user/hand/left/input/start",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_START_CLICK,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "Select",
        .paths =
            {
                "/user/hand/left/input/select/click",
                "/user/hand/left/input/select",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_SELECT_CLICK,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "Square™",
        .paths =
            {
                "/user/hand/left/input/square_mndx/click",
                "/user/hand/left/input/square_mndx",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_SQUARE_CLICK,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "Cross™",
        .paths =
            {
                "/user/hand/left/input/cross_mndx/click",
                "/user/hand/left/input/cross_mndx",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_CROSS_CLICK,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "Circle™",
        .paths =
            {
                "/user/hand/left/input/circle_mndx/click",
                "/user/hand/left/input/circle_mndx",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_CIRCLE_CLICK,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "Trinangle™",
        .paths =
            {
                "/user/hand/left/input/triangle_mndx/click",
                "/user/hand/left/input/triangle_mndx",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_TRIANGLE_CLICK,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "Trigger",
        .paths =
            {
                "/user/hand/left/input/trigger/value",
                "/user/hand/left/input/trigger",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_TRIGGER_VALUE,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "Grip",
        .paths =
            {
                "/user/hand/left/input/grip/pose",
                "/user/hand/left/input/grip",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_GRIP_POSE,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "Ball",
        .paths =
            {
                "/user/hand/left/input/ball_mndx/pose",
                "/user/hand/left/input/ball_mndx",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_BALL_CENTER_POSE,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "Aim",
        .paths =
            {
                "/user/hand/left/input/aim/pose",
                "/user/hand/left/input/aim",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_AIM_POSE,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .localized_name = "Haptic",
        .paths =
            {
                "/user/hand/left/output/haptic",
                NULL,
            },
        .outputs =
            {
                XRT_OUTPUT_NAME_PSMV_RUMBLE_VIBRATION,
                (enum xrt_output_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "PS™ Logo",
        .paths =
            {
                "/user/hand/right/input/system/click",
                "/user/hand/right/input/system",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_PS_CLICK,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "Move™ Logo",
        .paths =
            {
                "/user/hand/right/input/menu/click",
                "/user/hand/right/input/menu",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_MOVE_CLICK,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "Start/Options",
        .paths =
            {
                "/user/hand/right/input/start/click",
                "/user/hand/right/input/start",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_START_CLICK,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "Select",
        .paths =
            {
                "/user/hand/right/input/select/click",
                "/user/hand/right/input/select",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_SELECT_CLICK,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "Square™",
        .paths =
            {
                "/user/hand/right/input/square_mndx/click",
                "/user/hand/right/input/square_mndx",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_SQUARE_CLICK,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "Cross™",
        .paths =
            {
                "/user/hand/right/input/cross_mndx/click",
                "/user/hand/right/input/cross_mndx",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_CROSS_CLICK,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "Circle™",
        .paths =
            {
                "/user/hand/right/input/circle_mndx/click",
                "/user/hand/right/input/circle_mndx",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_CIRCLE_CLICK,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "Triangle™",
        .paths =
            {
                "/user/hand/right/input/triangle_mndx/click",
                "/user/hand/right/input/triangle_mndx",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_TRIANGLE_CLICK,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "Trigger",
        .paths =
            {
                "/user/hand/right/input/trigger/value",
                "/user/hand/right/input/trigger",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_TRIGGER_VALUE,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "Grip",
        .paths =
            {
                "/user/hand/right/input/grip/pose",
                "/user/hand/right/input/grip",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_GRIP_POSE,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "Ball",
        .paths =
            {
                "/user/hand/right/input/ball_mndx/pose",
                "/user/hand/right/input/ball_mndx",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_BALL_CENTER_POSE,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "Aim",
        .paths =
            {
                "/user/hand/right/input/aim/pose",
                "/user/hand/right/input/aim",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_AIM_POSE,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .localized_name = "Haptic",
        .paths =
            {
                "/user/hand/right/output/haptic",
                NULL,
            },
        .outputs =
            {
                XRT_OUTPUT_NAME_PSMV_RUMBLE_VIBRATION,
                (enum xrt_output_name)0,
            },
    },
};


#define MAKE_INPUT(COMPONENT, NAME, SUFFIX, INPUT)                             \
	{                                                                      \
	    .sub_path = OXR_SUB_ACTION_PATH_LEFT,                              \
	    .localized_name = NAME,                                            \
	    .paths =                                                           \
	        {                                                              \
	            "/user/hand/left/input/" #COMPONENT "/" #SUFFIX,           \
	            "/user/hand/left/input/" #COMPONENT,                       \
	            NULL,                                                      \
	        },                                                             \
	    .inputs =                                                          \
	        {                                                              \
	            INPUT,                                                     \
	            (enum xrt_input_name)0,                                    \
	        },                                                             \
	},                                                                     \
	    {                                                                  \
	        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,                         \
	        .localized_name = NAME,                                        \
	        .paths =                                                       \
	            {                                                          \
	                "/user/hand/right/input/" #COMPONENT "/" #SUFFIX,      \
	                "/user/hand/right/input/" #COMPONENT,                  \
	                NULL,                                                  \
	            },                                                         \
	        .inputs =                                                      \
	            {                                                          \
	                INPUT,                                                 \
	                (enum xrt_input_name)0,                                \
	            },                                                         \
	    },

// creates an input that can not be "downgraded" to the top level path.
// e.g. don't bind ../trackpad/click, ../trackpad/touch with just ../trackpad
#define MAKE_INPUT_SUFFIX_ONLY(COMPONENT, NAME, SUFFIX, INPUT)                 \
	{                                                                      \
	    .sub_path = OXR_SUB_ACTION_PATH_LEFT,                              \
	    .localized_name = NAME,                                            \
	    .paths =                                                           \
	        {                                                              \
	            "/user/hand/left/input/" #COMPONENT "/" #SUFFIX,           \
	            NULL,                                                      \
	        },                                                             \
	    .inputs =                                                          \
	        {                                                              \
	            INPUT,                                                     \
	            (enum xrt_input_name)0,                                    \
	        },                                                             \
	},                                                                     \
	    {                                                                  \
	        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,                         \
	        .localized_name = NAME,                                        \
	        .paths =                                                       \
	            {                                                          \
	                "/user/hand/right/input/" #COMPONENT "/" #SUFFIX,      \
	                NULL,                                                  \
	            },                                                         \
	        .inputs =                                                      \
	            {                                                          \
	                INPUT,                                                 \
	                (enum xrt_input_name)0,                                \
	            },                                                         \
	    },

// creates an input with a top level path and /x and /y sub paths
#define MAKE_INPUT_VEC2F(COMPONENT, NAME, INPUT)                               \
	{                                                                      \
	    .sub_path = OXR_SUB_ACTION_PATH_LEFT,                              \
	    .localized_name = NAME,                                            \
	    .paths =                                                           \
	        {                                                              \
	            "/user/hand/left/input/" #COMPONENT,                       \
	            "/user/hand/left/input/" #COMPONENT "/x",                  \
	            "/user/hand/left/input/" #COMPONENT "/y",                  \
	            NULL,                                                      \
	        },                                                             \
	    .inputs =                                                          \
	        {                                                              \
	            INPUT,                                                     \
	            (enum xrt_input_name)0,                                    \
	        },                                                             \
	},                                                                     \
	    {                                                                  \
	        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,                         \
	        .localized_name = NAME,                                        \
	        .paths =                                                       \
	            {                                                          \
	                "/user/hand/right/input/" #COMPONENT,                  \
	                "/user/hand/right/input/" #COMPONENT "/x",             \
	                "/user/hand/right/input/" #COMPONENT "/y",             \
	                NULL,                                                  \
	            },                                                         \
	        .inputs =                                                      \
	            {                                                          \
	                INPUT,                                                 \
	                (enum xrt_input_name)0,                                \
	            },                                                         \
	    },

#define MAKE_OUTPUT(COMPONENT, NAME, OUTPUT)                                   \
	{                                                                      \
	    .sub_path = OXR_SUB_ACTION_PATH_LEFT,                              \
	    .localized_name = NAME,                                            \
	    .paths =                                                           \
	        {                                                              \
	            "/user/hand/left/output/" #COMPONENT,                      \
	            NULL,                                                      \
	        },                                                             \
	    .outputs =                                                         \
	        {                                                              \
	            OUTPUT,                                                    \
	            (enum xrt_output_name)0,                                   \
	        },                                                             \
	},                                                                     \
	    {                                                                  \
	        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,                         \
	        .localized_name = NAME,                                        \
	        .paths =                                                       \
	            {                                                          \
	                "/user/hand/right/output/" #COMPONENT,                 \
	                NULL,                                                  \
	            },                                                         \
	        .outputs =                                                     \
	            {                                                          \
	                OUTPUT,                                                \
	                (enum xrt_output_name)0,                               \
	            },                                                         \
	    },

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

static struct profile_template profiles[5] = {
    {
        .path = "/interaction_profiles/khr/simple_controller",
        .localized_name = "Simple Controller",
        .bindings = khr_simple_controller_bindings,
        .num_bindings = ARRAY_SIZE(khr_simple_controller_bindings),
    },
    {
        .path = "/interaction_profiles/google/daydream_controller",
        .localized_name = "Daydream Controller",
        .bindings = google_daydream_controller_bindings,
        .num_bindings = ARRAY_SIZE(google_daydream_controller_bindings),
    },
    {
        .path = "/interaction_profiles/mndx/ball_on_a_stick_controller",
        .localized_name = "PS Move",
        .bindings = mndx_ball_on_a_stick_controller_bindings,
        .num_bindings = ARRAY_SIZE(mndx_ball_on_a_stick_controller_bindings),
    },
    {
        .path = "/interaction_profiles/valve/index_controller",
        .localized_name = "Index Controller",
        .bindings = valve_index_controller_bindings,
        .num_bindings = ARRAY_SIZE(valve_index_controller_bindings),
    },
    {
        .path = "/interaction_profiles/htc/vive_controller",
        .localized_name = "Vive Wand",
        .bindings = htc_vive_controller_bindings,
        .num_bindings = ARRAY_SIZE(htc_vive_controller_bindings),
    },
};
