/*
 * Copyright 2013, Fredrik Hultin.
 * Copyright 2013, Jakob Bornecrantz.
 * Copyright 2016 Philipp Zabel
 * Copyright 2019-2022 Jan Schmidt
 * Copyright 2022-2023 Max Thomas
 * SPDX-License-Identifier: BSL-1.0
 *
 */
/*!
 * @file
 * @brief  Translation layer from XRSP hand pose samples to OpenXR
 *
 * Glue code from sampled XRSP hand poses OpenXR poses
 *
 * @author Max Thomas <mtinc2@gmail.com>
 * @ingroup drv_quest_link
 */

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

#include "ql_hands.h"
#include "ql_system.h"

enum quest_hands_input_index
{
    /* Left controller */
    QUEST_HAND_L = 0,
    QUEST_HAND_R,

    INPUT_INDICES_LAST
};

int xrt_to_ovr[XRT_HAND_JOINT_COUNT] = 
{
    OVR_HAND_JOINT_WRIST, // OVR_HAND_JOINT_PALM
    OVR_HAND_JOINT_WRIST,

    OVR_HAND_JOINT_THUMB_METACARPAL,
    OVR_HAND_JOINT_THUMB_PROXIMAL,
    OVR_HAND_JOINT_THUMB_DISTAL,
    OVR_HAND_JOINT_THUMB_TIP,

    OVR_HAND_JOINT_WRIST, // OVR_HAND_JOINT_INDEX_METACARPAL,
    OVR_HAND_JOINT_INDEX_PROXIMAL,
    OVR_HAND_JOINT_INDEX_INTERMEDIATE,
    OVR_HAND_JOINT_INDEX_DISTAL,
    OVR_HAND_JOINT_INDEX_TIP,

    OVR_HAND_JOINT_WRIST, // OVR_HAND_JOINT_MIDDLE_METACARPAL,
    OVR_HAND_JOINT_MIDDLE_PROXIMAL,
    OVR_HAND_JOINT_MIDDLE_INTERMEDIATE,
    OVR_HAND_JOINT_MIDDLE_DISTAL,
    OVR_HAND_JOINT_MIDDLE_TIP,

    OVR_HAND_JOINT_WRIST, // OVR_HAND_JOINT_RING_METACARPAL,
    OVR_HAND_JOINT_RING_PROXIMAL,
    OVR_HAND_JOINT_RING_INTERMEDIATE,
    OVR_HAND_JOINT_RING_DISTAL,
    OVR_HAND_JOINT_RING_TIP,

    OVR_HAND_JOINT_LITTLE_METACARPAL,
    OVR_HAND_JOINT_LITTLE_PROXIMAL,
    OVR_HAND_JOINT_LITTLE_INTERMEDIATE,
    OVR_HAND_JOINT_LITTLE_DISTAL,
    OVR_HAND_JOINT_LITTLE_TIP,
};

enum xrt_space_relation_flags valid_flags = (enum xrt_space_relation_flags)(
    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
    XRT_SPACE_RELATION_POSITION_VALID_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT);

static void
ql_update_inputs(struct xrt_device *xdev)
{}

static xrt_vec3 nearest_pt_between(const xrt_vec3* p_a1, const xrt_vec3* p_a2, const xrt_vec3* p_b1, const xrt_vec3* p_b2)
{
    xrt_vec3 tmp_a1 = *p_a1;
    xrt_vec3 tmp_a2 = *p_a2;
    xrt_vec3 tmp_b1 = *p_b1;
    xrt_vec3 tmp_b2 = *p_b2;

    // Line AB represented as a1x + b1y = c1
    float a1 = tmp_a2.z - tmp_a1.z;
    float b1 = tmp_a1.x - tmp_a2.x;
    float c1 = a1*(tmp_a1.x) + b1*(tmp_a1.z);

    // Line CD represented as a2x + b2y = c2
    float a2 = tmp_b2.z - tmp_b1.z;
    float b2 = tmp_b1.x - tmp_b2.x;
    float c2 = a2*(tmp_b1.x)+ b2*(tmp_b1.z);

    float determinant = a1*b2 - a2*b1;

    if (determinant == 0)
    {
        // The lines are parallel
        return {0, 0, 0};
    }
    else
    {
        float x = (b2*c1 - b1*c2)/determinant;
        float z = (a1*c2 - a2*c1)/determinant;
        xrt_vec3 tmp = {x, (tmp_a1.y+tmp_a2.y+tmp_b1.y+tmp_b2.y)/4, z};

        return tmp;
    }

}

#define HAND_POSITION_IDX(which) (&out_tmp.values.hand_joint_set_default[which].relation.pose.position)
#define HAND_POSITION(WHICH) (&out_tmp.values.hand_joint_set_default[XRT_HAND_JOINT_##WHICH].relation.pose.position)

#define HAND_ORIENT_IDX(which) (&out_tmp.values.hand_joint_set_default[which].relation.pose.orientation)
#define HAND_ORIENT(WHICH) (&out_tmp.values.hand_joint_set_default[XRT_HAND_JOINT_##WHICH].relation.pose.orientation)


static void
ql_get_hand_tracking(struct xrt_device *xdev,
                     enum xrt_input_name name,
                     uint64_t at_timestamp_ns,
                     struct xrt_hand_joint_set *out_value,
                     uint64_t *out_timestamp_ns)
{
    struct ql_hands *ctrl = (struct ql_hands *)(xdev);

    if (name != XRT_INPUT_GENERIC_HAND_TRACKING_LEFT && name != XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT) {
        QUEST_LINK_ERROR("unknown input name for hand tracker");
        return;
    }

    struct ql_xrsp_host *host = &ctrl->sys->xrsp_host;
    os_mutex_lock(&host->pose_mutex);

    struct xrt_hand_joint_set out_tmp;
    bool hand_index = (name == XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT); // 0 if left, 1 if right.
    bool hand_valid = true;

    memset(&out_tmp, 0, sizeof(out_tmp));
    m_space_relation_ident(&out_tmp.hand_pose);


    // In hand space:
    for (int i = 0; i < XRT_HAND_JOINT_COUNT; i++) {
        int idx = xrt_to_ovr[i];

        out_tmp.values.hand_joint_set_default[i].relation.relation_flags = valid_flags;
        
        if (i > 0) {
            *HAND_POSITION_IDX(i) = ctrl->bones_last[(hand_index*24)+idx].pos;
            *HAND_ORIENT_IDX(i) = ctrl->bones_last[(hand_index*24)+idx].orient;
        }
        
        out_tmp.values.hand_joint_set_default[i].radius = 0.015; // 15mm 
    }

    // The Oculus hand spec did not have these, so we approximate them by intersecting the
    // proximal-wrist line segment with the thumb-pinky metacarpal line segments.
    *HAND_POSITION(INDEX_METACARPAL) = nearest_pt_between(HAND_POSITION(LITTLE_METACARPAL), 
                                                          HAND_POSITION(THUMB_METACARPAL), 
                                                          HAND_POSITION(WRIST), 
                                                          HAND_POSITION(INDEX_PROXIMAL));
    *HAND_POSITION(MIDDLE_METACARPAL) = nearest_pt_between(HAND_POSITION(LITTLE_METACARPAL), 
                                                          HAND_POSITION(THUMB_METACARPAL), 
                                                          HAND_POSITION(WRIST), 
                                                          HAND_POSITION(MIDDLE_PROXIMAL));
    *HAND_POSITION(RING_METACARPAL) = nearest_pt_between(HAND_POSITION(LITTLE_METACARPAL), 
                                                          HAND_POSITION(THUMB_METACARPAL), 
                                                          HAND_POSITION(WRIST), 
                                                          HAND_POSITION(RING_PROXIMAL));

    // The OpenXR spec says this is supposed to be on the middle finger bone
    *HAND_POSITION(PALM) = (*HAND_POSITION(MIDDLE_METACARPAL) + *HAND_POSITION(MIDDLE_PROXIMAL)) / 2;

    // Copy the orientation from the pinky for now
    *HAND_ORIENT(INDEX_METACARPAL) = *HAND_ORIENT(LITTLE_METACARPAL);
    *HAND_ORIENT(MIDDLE_METACARPAL) = *HAND_ORIENT(LITTLE_METACARPAL);
    *HAND_ORIENT(RING_METACARPAL) = *HAND_ORIENT(LITTLE_METACARPAL);

    // Palm orientation is the same as the wrist
    *HAND_ORIENT(PALM) = *HAND_ORIENT(WRIST);

    // We have to fiddle with the y basis on the left hand.
    if (hand_index == 0) {
        for (int i = 0; i < XRT_HAND_JOINT_COUNT; i++) {
            xrt_quat tmp;
            xrt_quat tmp2 = *HAND_ORIENT_IDX(i);
            xrt_vec3 x_vec = XRT_VEC3_UNIT_X;
            xrt_vec3 y_vec = XRT_VEC3_UNIT_Y;
            xrt_matrix_3x3 mat;

            math_matrix_3x3_from_quat(&tmp2, &mat);

            xrt_matrix_3x3 mat2 = {
                {mat.v[2], -mat.v[1], mat.v[0],
                 mat.v[5], -mat.v[4], mat.v[3],
                 mat.v[8], -mat.v[7], mat.v[6]}
             };

            for (int j = 0; j < 9; j++) {
                if (mat2.v[j] == -0.0) {
                    mat2.v[j] = 0.0;
                }
            }
            math_quat_from_matrix_3x3(&mat2, &tmp2);
            
            // Last adjustment, rotate the xz basis a bit.
            math_quat_from_angle_vector(90.0 * M_PI / 180, &y_vec, &tmp);
            math_quat_rotate(&tmp2, &tmp, &tmp2);

            *HAND_ORIENT_IDX(i) = tmp2;
        }
    }

    // For some reason Oculus points the xz basis towards the fingertips,
    // so we adjust them to have just the z basis pointing away from the
    // fingertips.
    for (int i = 0; i < XRT_HAND_JOINT_COUNT; i++) {
        xrt_quat tmp;
        xrt_quat tmp2 = *HAND_ORIENT_IDX(i);
        xrt_vec3 y_vec = XRT_VEC3_UNIT_Y;

        math_quat_from_angle_vector((45.0+90.0) * M_PI / 180, &y_vec, &tmp);
        math_quat_rotate(&tmp2, &tmp, &tmp2);

        *HAND_ORIENT_IDX(i) = tmp2;
    }

    // Transform to world space
    for (int i = 0; i < XRT_HAND_JOINT_COUNT; i++) {
        math_quat_rotate_vec3(&ctrl->poses[hand_index].orientation, HAND_POSITION_IDX(i), HAND_POSITION_IDX(i));
        math_vec3_accum(&ctrl->poses[hand_index].position, HAND_POSITION_IDX(i));
        math_quat_rotate(&ctrl->poses[hand_index].orientation, HAND_ORIENT_IDX(i), HAND_ORIENT_IDX(i));
    }

    if (hand_valid) {
        out_tmp.is_active = true;
        out_tmp.hand_pose.relation_flags = valid_flags;
    } else {
        out_tmp.is_active = false;
    }
    // This is a lie - this driver does no pose-prediction or history. Patches welcome.
    *out_timestamp_ns = at_timestamp_ns;
    *out_value = out_tmp;
    os_mutex_unlock(&host->pose_mutex);
}

static void
ql_hands_destroy(struct xrt_device *xdev)
{
    struct ql_hands *ctrl = (struct ql_hands *)(xdev);

    DRV_TRACE_MARKER();

    // Drop the reference to the system
    ql_system_reference(&ctrl->sys, NULL);

    u_var_remove_root(ctrl);

    u_device_free(&ctrl->base);
}

struct ql_hands *
ql_hands_create(struct ql_system *sys)
{
    DRV_TRACE_MARKER();

    enum u_device_alloc_flags flags =
        (enum u_device_alloc_flags)(U_DEVICE_ALLOC_TRACKING_NONE);

    int num_hands = 2;

    struct ql_hands *ctrl = U_DEVICE_ALLOCATE(struct ql_hands, flags, num_hands, 0);
    if (ctrl == NULL) {
        return NULL;
    }

    // Take a reference to the ql_system
    ql_system_reference(&ctrl->sys, sys);

    ctrl->base.tracking_origin = &sys->base;

    ctrl->base.update_inputs = ql_update_inputs;
    ctrl->base.get_hand_tracking = ql_get_hand_tracking;
    ctrl->base.destroy = ql_hands_destroy;

    ctrl->base.inputs[0].name = XRT_INPUT_GENERIC_HAND_TRACKING_LEFT;
    ctrl->base.inputs[1].name = XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT;

    ctrl->base.name = XRT_DEVICE_HAND_TRACKER;
    ctrl->base.device_type = XRT_DEVICE_TYPE_HAND_TRACKER;
    ctrl->base.hand_tracking_supported = true;

    ctrl->created_ns = os_monotonic_get_ns();

    QUEST_LINK_DEBUG("Meta Quest Link hands initialised.");

    return ctrl;

cleanup:
    ql_system_reference(&ctrl->sys, NULL);
    return NULL;
}
