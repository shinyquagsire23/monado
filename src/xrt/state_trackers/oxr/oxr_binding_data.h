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
	const char *paths[8];
	enum xrt_input_name inputs[8];
	enum xrt_output_name outputs[8];
};

struct profile_template
{
	const char *path;



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
        .paths =
            {
                "/user/hand/left/input/select/click",
                "/user/hand/left/input/select",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_TRIGGER_VALUE,
                XRT_INPUT_HYDRA_TRIGGER_VALUE,
                XRT_INPUT_DAYDREAM_TOUCHPAD_CLICK,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .paths =
            {
                "/user/hand/left/input/menu/click",
                "/user/hand/left/input/menu",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_MOVE_CLICK,
                XRT_INPUT_HYDRA_MIDDLE_CLICK,
                XRT_INPUT_DAYDREAM_BAR_CLICK,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .paths =
            {
                "/user/hand/left/input/grip/pose",
                "/user/hand/left/input/grip",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_BODY_CENTER_POSE,
                XRT_INPUT_HYDRA_POSE,
                XRT_INPUT_DAYDREAM_POSE,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .paths =
            {
                "/user/hand/left/input/aim/pose",
                "/user/hand/left/input/aim",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_BALL_TIP_POSE,
                XRT_INPUT_HYDRA_POSE,
                XRT_INPUT_DAYDREAM_POSE,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
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
        .paths =
            {
                "/user/hand/right/input/select/click",
                "/user/hand/right/input/select",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_TRIGGER_VALUE,
                XRT_INPUT_HYDRA_TRIGGER_VALUE,
                XRT_INPUT_DAYDREAM_TOUCHPAD_CLICK,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .paths =
            {
                "/user/hand/right/input/menu/click",
                "/user/hand/right/input/menu",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_MOVE_CLICK,
                XRT_INPUT_HYDRA_MIDDLE_CLICK,
                XRT_INPUT_DAYDREAM_BAR_CLICK,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .paths =
            {
                "/user/hand/right/input/grip/pose",
                "/user/hand/right/input/grip",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_BODY_CENTER_POSE,
                XRT_INPUT_HYDRA_POSE,
                XRT_INPUT_DAYDREAM_POSE,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .paths =
            {
                "/user/hand/right/input/aim/pose",
                "/user/hand/right/input/aim",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_BALL_TIP_POSE,
                XRT_INPUT_HYDRA_POSE,
                XRT_INPUT_DAYDREAM_POSE,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
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
        .paths =
            {
                "/user/hand/left/input/grip/pose",
                "/user/hand/left/input/grip",
                NULL,
            },
        .inputs =
            {
#if 0
                XRT_INPUT_PSMV_BODY_CENTER_POSE,
                XRT_INPUT_HYDRA_POSE,
#endif
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .paths =
            {
                "/user/hand/left/input/aim/pose",
                "/user/hand/left/input/aim",
                NULL,
            },
        .inputs =
            {
#if 0
                XRT_INPUT_PSMV_BALL_TIP_POSE,
                XRT_INPUT_HYDRA_POSE,
#endif
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
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
        //! @todo Flag that this is a trackpad
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
        .paths =
            {
                "/user/hand/right/input/grip/pose",
                "/user/hand/right/input/grip",
                NULL,
            },
        .inputs =
            {
#if 0
                XRT_INPUT_PSMV_BODY_CENTER_POSE,
                XRT_INPUT_HYDRA_POSE,
#endif
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .paths =
            {
                "/user/hand/right/input/aim/pose",
                "/user/hand/right/input/aim",
                NULL,
            },
        .inputs =
            {
#if 0
                XRT_INPUT_PSMV_BALL_TIP_POSE,
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

static struct binding_template mnd_ball_on_stick_controller_bindings[26] = {
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
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
        .paths =
            {
                "/user/hand/left/input/square_mnd/click",
                "/user/hand/left/input/square_mnd",
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
        .paths =
            {
                "/user/hand/left/input/cross_mnd/click",
                "/user/hand/left/input/cross_mnd",
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
        .paths =
            {
                "/user/hand/left/input/circle_mnd/click",
                "/user/hand/left/input/circle_mnd",
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
        .paths =
            {
                "/user/hand/left/input/triangle_mnd/click",
                "/user/hand/left/input/triangle_mnd",
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
        .paths =
            {
                "/user/hand/left/input/grip/pose",
                "/user/hand/left/input/grip",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_BODY_CENTER_POSE,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .paths =
            {
                "/user/hand/left/input/ball_mnd/pose",
                "/user/hand/left/input/ball_mnd",
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
        .paths =
            {
                "/user/hand/left/input/aim/pose",
                "/user/hand/left/input/aim",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_BALL_TIP_POSE,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
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
        .paths =
            {
                "/user/hand/right/input/square_mnd/click",
                "/user/hand/right/input/square_mnd",
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
        .paths =
            {
                "/user/hand/right/input/cross_mnd/click",
                "/user/hand/right/input/cross_mnd",
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
        .paths =
            {
                "/user/hand/right/input/circle_mnd/click",
                "/user/hand/right/input/circle_mnd",
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
        .paths =
            {
                "/user/hand/right/input/triangle_mnd/click",
                "/user/hand/right/input/triangle_mnd",
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
        .paths =
            {
                "/user/hand/right/input/grip/pose",
                "/user/hand/right/input/grip",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_BODY_CENTER_POSE,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .paths =
            {
                "/user/hand/right/input/ball_mnd/pose",
                "/user/hand/right/input/ball_mnd",
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
        .paths =
            {
                "/user/hand/right/input/aim/pose",
                "/user/hand/right/input/aim",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_BALL_TIP_POSE,
                (enum xrt_input_name)0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
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

static struct profile_template profiles[3] = {
    {
        .path = "/interaction_profiles/khr/simple_controller",
        .bindings = khr_simple_controller_bindings,
        .num_bindings = ARRAY_SIZE(khr_simple_controller_bindings),
    },
    {
        .path = "/interaction_profiles/google/daydream_controller",
        .bindings = google_daydream_controller_bindings,
        .num_bindings = ARRAY_SIZE(google_daydream_controller_bindings),
    },
    {
        .path = "/interaction_profiles/mnd/ball_on_stick_controller",
        .bindings = mnd_ball_on_stick_controller_bindings,
        .num_bindings = ARRAY_SIZE(mnd_ball_on_stick_controller_bindings),
    },
};
