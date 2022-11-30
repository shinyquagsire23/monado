/*
 * Copyright 2013, Fredrik Hultin.
 * Copyright 2013, Jakob Bornecrantz.
 * Copyright 2016 Philipp Zabel
 * Copyright 2019-2022 Jan Schmidt
 * SPDX-License-Identifier: BSL-1.0
 *
 */
/*!
 * @file
 * @brief  Driver code for Meta Quest Link headsets
 *
 * Implementation for the HMD communication, calibration and
 * IMU integration.
 *
 * Ported from OpenHMD
 *
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_quest_link
 */

/* Meta Quest Link Driver - HID/USB Driver Implementation */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <assert.h>
#include <inttypes.h>

#include "math/m_api.h"
#include "math/m_vec3.h"
#include "math/m_space.h"
#include "math/m_predict.h"

#include "os/os_time.h"

#include "util/u_device.h"
#include "util/u_trace_marker.h"
#include "util/u_time.h"
#include "util/u_var.h"

#include "xrt/xrt_device.h"

#include "ql_controller.h"
#include "ql_system.h"


static struct xrt_binding_input_pair simple_inputs_ql[4] = {
    {XRT_INPUT_SIMPLE_SELECT_CLICK, XRT_INPUT_TOUCH_TRIGGER_VALUE},
    {XRT_INPUT_SIMPLE_MENU_CLICK, XRT_INPUT_TOUCH_MENU_CLICK},
    {XRT_INPUT_SIMPLE_GRIP_POSE, XRT_INPUT_TOUCH_GRIP_POSE},
    {XRT_INPUT_SIMPLE_AIM_POSE, XRT_INPUT_TOUCH_AIM_POSE},
};

static struct xrt_binding_output_pair simple_outputs_ql[1] = {
    {XRT_OUTPUT_NAME_SIMPLE_VIBRATION, XRT_OUTPUT_NAME_TOUCH_HAPTIC},
};

static struct xrt_binding_profile binding_profiles_ql[1] = {
    {
        .name = XRT_DEVICE_SIMPLE_CONTROLLER,
        .inputs = simple_inputs_ql,
        .input_count = ARRAY_SIZE(simple_inputs_ql),
        .outputs = simple_outputs_ql,
        .output_count = ARRAY_SIZE(simple_outputs_ql),
    },
};

enum touch_controller_input_index
{
    /* Left controller */
    OCULUS_TOUCH_X_CLICK = 0,
    OCULUS_TOUCH_X_TOUCH,
    OCULUS_TOUCH_Y_CLICK,
    OCULUS_TOUCH_Y_TOUCH,
    OCULUS_TOUCH_MENU_CLICK,

    /* Right controller */
    OCULUS_TOUCH_A_CLICK = 0,
    OCULUS_TOUCH_A_TOUCH,
    OCULUS_TOUCH_B_CLICK,
    OCULUS_TOUCH_B_TOUCH,
    OCULUS_TOUCH_SYSTEM_CLICK,

    /* Common */
    OCULUS_TOUCH_SQUEEZE_VALUE,
    OCULUS_TOUCH_TRIGGER_TOUCH,
    OCULUS_TOUCH_TRIGGER_VALUE,
    OCULUS_TOUCH_THUMBSTICK_CLICK,
    OCULUS_TOUCH_THUMBSTICK_TOUCH,
    OCULUS_TOUCH_THUMBSTICK,
    OCULUS_TOUCH_THUMBREST_TOUCH,
    OCULUS_TOUCH_GRIP_POSE,
    OCULUS_TOUCH_AIM_POSE,

    INPUT_INDICES_LAST
};

#define SET_TOUCH_INPUT(d, NAME) ((d)->base.inputs[OCULUS_TOUCH_##NAME].name = XRT_INPUT_TOUCH_##NAME)
#define DEBUG_TOUCH_INPUT_BOOL(d, NAME, label)                                                                         \
    u_var_add_bool((d), &(d)->base.inputs[OCULUS_TOUCH_##NAME].value.boolean, label)
#define DEBUG_TOUCH_INPUT_F32(d, NAME, label)                                                                          \
    u_var_add_f32((d), &(d)->base.inputs[OCULUS_TOUCH_##NAME].value.vec1.x, label)
#define DEBUG_TOUCH_INPUT_VEC2(d, NAME, label1, label2)                                                                \
    u_var_add_f32((d), &(d)->base.inputs[OCULUS_TOUCH_##NAME].value.vec2.x, label1);                               \
    u_var_add_f32((d), &(d)->base.inputs[OCULUS_TOUCH_##NAME].value.vec2.y, label2)


static void
ql_update_input_bool(struct ql_controller *ctrl, int index, int64_t when_ns, int val)
{
    ctrl->base.inputs[index].timestamp = when_ns;
    ctrl->base.inputs[index].value.boolean = (val != 0);
}

static void
ql_update_input_analog(struct ql_controller *ctrl, int index, int64_t when_ns, float val)
{
    ctrl->base.inputs[index].timestamp = when_ns;
    ctrl->base.inputs[index].value.vec1.x = val;
}

static void
ql_update_input_vec2(struct ql_controller *ctrl, int index, int64_t when_ns, float x, float y)
{
    ctrl->base.inputs[index].timestamp = when_ns;
    ctrl->base.inputs[index].value.vec2.x = x;
    ctrl->base.inputs[index].value.vec2.y = y;
}

static void
ql_update_inputs(struct xrt_device *xdev)
{
    struct ql_controller *ctrl = (struct ql_controller *)(xdev);

    if (ctrl->features & OVR_TOUCH_FEAT_RIGHT) {
        ql_update_input_bool(ctrl, OCULUS_TOUCH_A_CLICK, ctrl->pose_ns, ctrl->buttons & OVR_TOUCH_BTN_A);
        ql_update_input_bool(ctrl, OCULUS_TOUCH_B_CLICK, ctrl->pose_ns, ctrl->buttons & OVR_TOUCH_BTN_B);
        ql_update_input_bool(ctrl, OCULUS_TOUCH_SYSTEM_CLICK, ctrl->pose_ns, ctrl->buttons & OVR_TOUCH_BTN_SYSTEM);

        ql_update_input_bool(ctrl, OCULUS_TOUCH_A_TOUCH, ctrl->pose_ns, ctrl->capacitance & OVR_TOUCH_CAP_A_X);
        ql_update_input_bool(ctrl, OCULUS_TOUCH_B_TOUCH, ctrl->pose_ns, ctrl->capacitance & OVR_TOUCH_CAP_B_Y);
    }
    else {
        ql_update_input_bool(ctrl, OCULUS_TOUCH_X_CLICK, ctrl->pose_ns, ctrl->buttons & OVR_TOUCH_BTN_X);
        ql_update_input_bool(ctrl, OCULUS_TOUCH_Y_CLICK, ctrl->pose_ns, ctrl->buttons & OVR_TOUCH_BTN_Y);
        ql_update_input_bool(ctrl, OCULUS_TOUCH_MENU_CLICK, ctrl->pose_ns, ctrl->buttons & OVR_TOUCH_BTN_MENU);

        ql_update_input_bool(ctrl, OCULUS_TOUCH_X_TOUCH, ctrl->pose_ns, ctrl->capacitance & OVR_TOUCH_CAP_A_X);
        ql_update_input_bool(ctrl, OCULUS_TOUCH_Y_TOUCH, ctrl->pose_ns, ctrl->capacitance & OVR_TOUCH_CAP_B_Y);
    }
    
    ql_update_input_analog(ctrl, OCULUS_TOUCH_SQUEEZE_VALUE, ctrl->pose_ns, ctrl->grip_z);
    ql_update_input_analog(ctrl, OCULUS_TOUCH_TRIGGER_VALUE, ctrl->pose_ns, ctrl->trigger_z);
    ql_update_input_bool(ctrl, OCULUS_TOUCH_TRIGGER_TOUCH, ctrl->pose_ns, ctrl->capacitance & OVR_TOUCH_CAP_TRIGGER);

    ql_update_input_bool(ctrl, OCULUS_TOUCH_THUMBSTICK_CLICK, ctrl->pose_ns, ctrl->buttons & OVR_TOUCH_BTN_STICKS);
    ql_update_input_bool(ctrl, OCULUS_TOUCH_THUMBSTICK_TOUCH, ctrl->pose_ns, ctrl->capacitance & OVR_TOUCH_CAP_STICK);

    ql_update_input_vec2(ctrl, OCULUS_TOUCH_THUMBSTICK, ctrl->pose_ns, ctrl->joystick_x, ctrl->joystick_y);
}

static void
ql_get_tracked_pose(struct xrt_device *xdev,
                        enum xrt_input_name name,
                        uint64_t at_timestamp_ns,
                        struct xrt_space_relation *out_relation)
{
    struct ql_controller *ctrl = (struct ql_controller *)(xdev);

    //printf("Get tracked %p %x\n", ctrl, name);
    if (name != XRT_INPUT_TOUCH_AIM_POSE && name != XRT_INPUT_TOUCH_GRIP_POSE) {
        QUEST_LINK_ERROR("Unknown input name");
        return;
    }

    struct xrt_space_relation relation;
    U_ZERO(&relation);

    relation.pose = ctrl->pose;
    relation.pose.position += ctrl->pose_add;
    relation.angular_velocity = ctrl->angvel;
    relation.linear_velocity = ctrl->vel;
    //ql_controller_get_interpolated_pose(ctrl, at_timestamp_ns, &ctrl->pose, &out_relation->pose);

    //out_relation->pose = ctrl->pose;
    relation.relation_flags = (enum xrt_space_relation_flags)(XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
                                                                   XRT_SPACE_RELATION_POSITION_VALID_BIT |
                                                                   XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);

    //ql_tracker_get_tracked_pose(ctrl->tracker, RIFT_S_TRACKER_POSE_DEVICE, at_timestamp_ns, out_relation);

    timepoint_ns prediction_ns = at_timestamp_ns - ctrl->pose_ns;
    double prediction_s = time_ns_to_s(prediction_ns);

    m_predict_relation(&relation, prediction_s, out_relation);
}

static void
ql_get_view_poses(struct xrt_device *xdev,
                      const struct xrt_vec3 *default_eye_relation,
                      uint64_t at_timestamp_ns,
                      uint32_t view_count,
                      struct xrt_space_relation *out_head_relation,
                      struct xrt_fov *out_fovs,
                      struct xrt_pose *out_poses)
{
    u_device_get_view_poses(xdev, default_eye_relation, at_timestamp_ns, view_count, out_head_relation, out_fovs,
                            out_poses);
}

static void
ql_set_output(struct xrt_device *xdev, enum xrt_output_name name, const union xrt_output_value *value)
{
    /* TODO: Implement haptic sending */
}

static void
ql_controller_destroy(struct xrt_device *xdev)
{
    struct ql_controller *ctrl = (struct ql_controller *)(xdev);

    DRV_TRACE_MARKER();

    /* Remove this device from the system */
    //ql_system_remove_controller(ctrl->sys);

    /* Drop the reference to the system */
    ql_system_reference(&ctrl->sys, NULL);

    u_var_remove_root(ctrl);

    u_device_free(&ctrl->base);
}

struct ql_controller *
ql_controller_create(struct ql_system *sys, enum xrt_device_type device_type)
{
    DRV_TRACE_MARKER();

    enum u_device_alloc_flags flags =
        (enum u_device_alloc_flags)(U_DEVICE_ALLOC_TRACKING_NONE);

    struct ql_controller *ctrl = U_DEVICE_ALLOCATE(struct ql_controller, flags, INPUT_INDICES_LAST, 1);
    if (ctrl == NULL) {
        return NULL;
    }

    /* Take a reference to the ql_system */
    ql_system_reference(&ctrl->sys, sys);

    ctrl->base.tracking_origin = &sys->base;

    ctrl->base.update_inputs = ql_update_inputs;
    ctrl->base.set_output = ql_set_output;
    ctrl->base.get_tracked_pose = ql_get_tracked_pose;
    ctrl->base.get_view_poses = ql_get_view_poses;
    ctrl->base.destroy = ql_controller_destroy;
    ctrl->base.name = XRT_DEVICE_TOUCH_CONTROLLER;
    ctrl->base.device_type = device_type;

    if (device_type == XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER) {
        snprintf(ctrl->base.str, XRT_DEVICE_NAME_LEN, "Quest Left Touch Controller");
        snprintf(ctrl->base.serial, XRT_DEVICE_NAME_LEN, "Left Controller");
        SET_TOUCH_INPUT(ctrl, X_CLICK);
        SET_TOUCH_INPUT(ctrl, X_TOUCH);
        SET_TOUCH_INPUT(ctrl, Y_CLICK);
        SET_TOUCH_INPUT(ctrl, Y_TOUCH);
        SET_TOUCH_INPUT(ctrl, MENU_CLICK);
        //ctrl->base.inputs[0].name = XRT_INPUT_GENERIC_HAND_TRACKING_LEFT;
    } else {
        snprintf(ctrl->base.str, XRT_DEVICE_NAME_LEN, "Quest Right Touch Controller");
        snprintf(ctrl->base.serial, XRT_DEVICE_NAME_LEN, "Right Controller");
        SET_TOUCH_INPUT(ctrl, A_CLICK);
        SET_TOUCH_INPUT(ctrl, A_TOUCH);
        SET_TOUCH_INPUT(ctrl, B_CLICK);
        SET_TOUCH_INPUT(ctrl, B_TOUCH);
        SET_TOUCH_INPUT(ctrl, SYSTEM_CLICK);
        //ctrl->base.inputs[0].name = XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT;
    }

    SET_TOUCH_INPUT(ctrl, SQUEEZE_VALUE);
    SET_TOUCH_INPUT(ctrl, TRIGGER_TOUCH);
    SET_TOUCH_INPUT(ctrl, TRIGGER_VALUE);
    SET_TOUCH_INPUT(ctrl, THUMBSTICK_CLICK);
    SET_TOUCH_INPUT(ctrl, THUMBSTICK_TOUCH);
    SET_TOUCH_INPUT(ctrl, THUMBSTICK);
    SET_TOUCH_INPUT(ctrl, THUMBREST_TOUCH);
    SET_TOUCH_INPUT(ctrl, GRIP_POSE);
    SET_TOUCH_INPUT(ctrl, AIM_POSE);

    ctrl->base.outputs[0].name = XRT_OUTPUT_NAME_TOUCH_HAPTIC;

    ctrl->base.binding_profiles = binding_profiles_ql;
    ctrl->base.binding_profile_count = ARRAY_SIZE(binding_profiles_ql);

    //ctrl->tracker = ql_system_get_tracker(sys);

    ctrl->created_ns = os_monotonic_get_ns();

    ctrl->pose.position.x = 0.0f;
    ctrl->pose.position.y = 0.0f;
    ctrl->pose.position.z = 0.0f;

    ctrl->pose.orientation.x = 0.0f;
    ctrl->pose.orientation.y = 0.0f;
    ctrl->pose.orientation.z = 0.0f;
    ctrl->pose.orientation.w = 1.0f;

    QUEST_LINK_DEBUG("Meta Quest Link controller initialised.");

    return ctrl;

cleanup:
    ql_system_reference(&ctrl->sys, NULL);
    return NULL;
}
