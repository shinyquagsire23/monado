// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Shared bindings structs for @ref drv_vive & @ref drv_survive.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_vive
 */

#include "vive/vive_bindings.h"

#include "xrt/xrt_device.h"


/*
 *
 * Index Controller
 *
 */

static struct xrt_binding_input_pair simple_inputs_index[4] = {
    {XRT_INPUT_SIMPLE_SELECT_CLICK, XRT_INPUT_INDEX_TRIGGER_VALUE},
    {XRT_INPUT_SIMPLE_MENU_CLICK, XRT_INPUT_INDEX_B_CLICK},
    {XRT_INPUT_SIMPLE_GRIP_POSE, XRT_INPUT_INDEX_GRIP_POSE},
    {XRT_INPUT_SIMPLE_AIM_POSE, XRT_INPUT_INDEX_AIM_POSE},
};

static struct xrt_binding_output_pair simple_outputs_index[1] = {
    {XRT_OUTPUT_NAME_SIMPLE_VIBRATION, XRT_OUTPUT_NAME_INDEX_HAPTIC},
};

// Exported to drivers.
struct xrt_binding_profile vive_binding_profiles_index[1] = {
    {
        .name = XRT_DEVICE_SIMPLE_CONTROLLER,
        .inputs = simple_inputs_index,
        .input_count = ARRAY_SIZE(simple_inputs_index),
        .outputs = simple_outputs_index,
        .output_count = ARRAY_SIZE(simple_outputs_index),
    },
};

uint32_t vive_binding_profiles_index_count = ARRAY_SIZE(vive_binding_profiles_index);


/*
 *
 * Vive Wand Controller
 *
 */

static struct xrt_binding_input_pair simple_inputs_wand[4] = {
    {XRT_INPUT_SIMPLE_SELECT_CLICK, XRT_INPUT_VIVE_TRIGGER_VALUE},
    {XRT_INPUT_SIMPLE_MENU_CLICK, XRT_INPUT_VIVE_MENU_CLICK},
    {XRT_INPUT_SIMPLE_GRIP_POSE, XRT_INPUT_VIVE_GRIP_POSE},
    {XRT_INPUT_SIMPLE_AIM_POSE, XRT_INPUT_VIVE_AIM_POSE},
};

static struct xrt_binding_output_pair simple_outputs_wand[1] = {
    {XRT_OUTPUT_NAME_SIMPLE_VIBRATION, XRT_OUTPUT_NAME_VIVE_HAPTIC},
};

// Exported to drivers.
struct xrt_binding_profile vive_binding_profiles_wand[1] = {
    {
        .name = XRT_DEVICE_SIMPLE_CONTROLLER,
        .inputs = simple_inputs_wand,
        .input_count = ARRAY_SIZE(simple_inputs_wand),
        .outputs = simple_outputs_wand,
        .output_count = ARRAY_SIZE(simple_outputs_wand),
    },
};

uint32_t vive_binding_profiles_wand_count = ARRAY_SIZE(vive_binding_profiles_wand);
