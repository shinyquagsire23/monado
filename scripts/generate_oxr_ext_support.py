#!/usr/bin/env python3
# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0
"""Simple script to update oxr_extension_support.h."""

from pathlib import Path

# Each extension that we implement gets an entry in this tuple.
# Each entry should be a list of defines that are checked for an extension:
# the first one must be the name of the extension itself.
# Keep sorted.
EXTENSIONS = (
    ['XR_KHR_android_create_instance', 'XR_USE_PLATFORM_ANDROID'],
    ['XR_KHR_convert_timespec_time', 'XR_USE_TIMESPEC'],
    ['XR_KHR_opengl_enable', 'XR_USE_GRAPHICS_API_OPENGL'],
    ['XR_KHR_opengl_es_enable', 'XR_USE_GRAPHICS_API_OPENGL_ES'],
    ['XR_KHR_vulkan_enable', 'XR_USE_GRAPHICS_API_VULKAN'],
    ['XR_KHR_vulkan_enable2', 'XR_USE_GRAPHICS_API_VULKAN'],
    ['XR_KHR_composition_layer_depth', 'XRT_FEATURE_OPENXR_LAYER_DEPTH'],
    ['XR_KHR_composition_layer_cube', 'XRT_FEATURE_OPENXR_LAYER_CUBE'],
    ['XR_KHR_composition_layer_cylinder', 'XRT_FEATURE_OPENXR_LAYER_CYLINDER'],
    ['XR_KHR_composition_layer_equirect', 'XRT_FEATURE_OPENXR_LAYER_EQUIRECT1'],
    ['XR_KHR_composition_layer_equirect2', 'XRT_FEATURE_OPENXR_LAYER_EQUIRECT2'],
    ['XR_EXT_debug_utils'],
    ['XR_MND_headless'],
    ['XR_MND_swapchain_usage_input_attachment_bit'],
    ['XR_EXTX_overlay'],
    ['XR_MNDX_egl_enable', 'XR_USE_PLATFORM_EGL', 'XR_USE_GRAPHICS_API_OPENGL'],
    ['XR_MNDX_ball_on_a_stick_controller'],
    ['XR_EXT_hand_tracking']
)

ROOT = Path(__file__).resolve().parent.parent
FN = ROOT / 'src' / 'xrt'/'state_trackers' / 'oxr' / 'oxr_extension_support.h'

INVOCATION_PREFIX = 'OXR_EXTENSION_SUPPORT'
BEGIN_OF_PER_EXTENSION = '// beginning of GENERATED defines - do not modify - used by scripts'
END_OF_PER_EXTENSION = '// end of GENERATED per-extension defines - do not modify - used by scripts'
CLANG_FORMAT_OFF = '// clang-format off'
CLANG_FORMAT_ON = '// clang-format on'


def trim_ext_name(name):
    return name[3:]


def generate_first_chunk():
    parts = []
    for data in EXTENSIONS:
        ext_name = data[0]
        trimmed_name = trim_ext_name(ext_name)
        upper_name = trimmed_name.upper()
        condition = " && ".join("defined({})".format(x) for x in data)

        parts.append(f"""
/*
* {ext_name}
*/
#if {condition}
#define OXR_HAVE_{trimmed_name}
#define {INVOCATION_PREFIX}_{trimmed_name}(_) \\
    _({trimmed_name}, {upper_name})
#else
#define {INVOCATION_PREFIX}_{trimmed_name}(_)
#endif
""")
    return '\n'.join(parts)


def generate_second_chunk():
    trimmed_names = [trim_ext_name(data[0]) for data in EXTENSIONS]
    invocations = ('{}_{}(_)'.format(INVOCATION_PREFIX, name)
                   for name in trimmed_names)

    macro_lines = ['#define OXR_EXTENSION_SUPPORT_GENERATE(_)']
    macro_lines.extend(invocations)

    lines = [CLANG_FORMAT_OFF]
    lines.append(' \\\n    '.join(macro_lines))
    lines.append(CLANG_FORMAT_ON)
    return '\n'.join(lines)


if __name__ == "__main__":
    with open(str(FN), 'r', encoding='utf-8') as fp:
        orig = [line.rstrip() for line in fp.readlines()]
    beginning = orig[:orig.index(BEGIN_OF_PER_EXTENSION)+1]
    middle_start = orig.index(END_OF_PER_EXTENSION)
    middle_end = orig.index(CLANG_FORMAT_OFF)
    middle = orig[middle_start:middle_end]

    new_contents = beginning
    new_contents.append(generate_first_chunk())
    new_contents.extend(middle)
    new_contents.append(generate_second_chunk())

    with open(str(FN), 'w', encoding='utf-8') as fp:
        fp.write('\n'.join(new_contents))
        fp.write('\n')
