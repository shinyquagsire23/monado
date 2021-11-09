#!/usr/bin/env python3
# Copyright 2019-2021, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0
"""Simple script to update vk_helpers.{c,h}."""

from pathlib import Path
from typing import List, Optional, Tuple

# Each tuple is a function name, followed optionally by one or more conditions
# to test in the preprocessor, which will be wrapped in "defined()"
# if they aren't already. Empty tuples insert a blank line.

DEVICE_FUNCTIONS = [
    ("vkDestroyDevice",),
    ("vkDeviceWaitIdle",),
    ("vkAllocateMemory",),
    ("vkFreeMemory",),
    ("vkMapMemory",),
    ("vkUnmapMemory",),
    (),
    ("vkCreateBuffer",),
    ("vkDestroyBuffer",),
    ("vkBindBufferMemory",),
    (),
    ("vkCreateImage",),
    ("vkDestroyImage",),
    ("vkBindImageMemory",),
    (),
    ("vkGetBufferMemoryRequirements",),
    ("vkFlushMappedMemoryRanges",),
    ("vkGetImageMemoryRequirements",),
    ("vkGetImageMemoryRequirements2KHR",),
    ("vkGetImageSubresourceLayout",),
    (),
    ("vkCreateImageView",),
    ("vkDestroyImageView",),
    (),
    ("vkCreateSampler",),
    ("vkDestroySampler",),
    (),
    ("vkCreateShaderModule",),
    ("vkDestroyShaderModule",),
    (),
    ("vkCreateCommandPool",),
    ("vkDestroyCommandPool",),
    (),
    ("vkAllocateCommandBuffers",),
    ("vkBeginCommandBuffer",),
    ("vkCmdPipelineBarrier",),
    ("vkCmdBeginRenderPass",),
    ("vkCmdSetScissor",),
    ("vkCmdSetViewport",),
    ("vkCmdClearColorImage",),
    ("vkCmdEndRenderPass",),
    ("vkCmdBindDescriptorSets",),
    ("vkCmdBindPipeline",),
    ("vkCmdBindVertexBuffers",),
    ("vkCmdBindIndexBuffer",),
    ("vkCmdDraw",),
    ("vkCmdDrawIndexed",),
    ("vkCmdDispatch",),
    ("vkCmdCopyBuffer",),
    ("vkCmdCopyBufferToImage",),
    ("vkCmdCopyImage",),
    ("vkCmdCopyImageToBuffer",),
    ("vkEndCommandBuffer",),
    ("vkFreeCommandBuffers",),
    (),
    ("vkCreateRenderPass",),
    ("vkDestroyRenderPass",),
    (),
    ("vkCreateFramebuffer",),
    ("vkDestroyFramebuffer",),
    (),
    ("vkCreatePipelineCache",),
    ("vkDestroyPipelineCache",),
    (),
    ("vkResetDescriptorPool",),
    ("vkCreateDescriptorPool",),
    ("vkDestroyDescriptorPool",),
    (),
    ("vkAllocateDescriptorSets",),
    ("vkFreeDescriptorSets",),
    (),
    ("vkCreateComputePipelines",),
    ("vkCreateGraphicsPipelines",),
    ("vkDestroyPipeline",),
    (),
    ("vkCreatePipelineLayout",),
    ("vkDestroyPipelineLayout",),
    (),
    ("vkCreateDescriptorSetLayout",),
    ("vkUpdateDescriptorSets",),
    ("vkDestroyDescriptorSetLayout",),
    (),
    ("vkGetDeviceQueue",),
    ("vkQueueSubmit",),
    ("vkQueueWaitIdle",),
    (),
    ("vkCreateSemaphore",),
    ("vkSignalSemaphoreKHR", "VK_KHR_timeline_semaphore"),
    ("vkDestroySemaphore",),
    (),
    ("vkCreateFence",),
    ("vkWaitForFences",),
    ("vkGetFenceStatus",),
    ("vkDestroyFence",),
    ("vkResetFences",),
    (),
    ("vkCreateSwapchainKHR",),
    ("vkDestroySwapchainKHR",),
    ("vkGetSwapchainImagesKHR",),
    ("vkAcquireNextImageKHR",),
    ("vkQueuePresentKHR",),
    (),
    ("vkGetMemoryWin32HandleKHR", "VK_USE_PLATFORM_WIN32_KHR"),
    ("vkImportSemaphoreWin32HandleKHR", "VK_USE_PLATFORM_WIN32_KHR"),
    ("vkImportFenceWin32HandleKHR", "VK_USE_PLATFORM_WIN32_KHR"),
    ("vkGetMemoryFdKHR", "!defined(VK_USE_PLATFORM_WIN32_KHR)"),
    (),
    ("vkImportSemaphoreFdKHR", "!defined(VK_USE_PLATFORM_WIN32_KHR)"),
    ("vkGetSemaphoreFdKHR", "!defined(VK_USE_PLATFORM_WIN32_KHR)"),
    (),
    ("vkImportFenceFdKHR", "!defined(VK_USE_PLATFORM_WIN32_KHR)"),
    ("vkGetFenceFdKHR", "!defined(VK_USE_PLATFORM_WIN32_KHR)"),
    ("vkGetMemoryAndroidHardwareBufferANDROID", "VK_USE_PLATFORM_ANDROID_KHR"),
    (
        "vkGetAndroidHardwareBufferPropertiesANDROID",
        "VK_USE_PLATFORM_ANDROID_KHR",
    ),
    (),
    ("vkGetPastPresentationTimingGOOGLE",),
]
INSTANCE_FUNCTIONS = [
    ("vkDestroyInstance",),
    ("vkGetDeviceProcAddr",),
    ("vkCreateDevice",),
    ("vkDestroySurfaceKHR",),
    (),
    ("vkCreateDebugReportCallbackEXT",),
    ("vkDestroyDebugReportCallbackEXT",),
    (),
    ("vkEnumeratePhysicalDevices",),
    ("vkGetPhysicalDeviceProperties",),
    ("vkGetPhysicalDeviceProperties2",),
    ("vkGetPhysicalDeviceFeatures2",),
    ("vkGetPhysicalDeviceMemoryProperties",),
    ("vkGetPhysicalDeviceQueueFamilyProperties",),
    ("vkGetPhysicalDeviceSurfaceCapabilitiesKHR",),
    ("vkGetPhysicalDeviceSurfaceFormatsKHR",),
    ("vkGetPhysicalDeviceSurfacePresentModesKHR",),
    ("vkGetPhysicalDeviceSurfaceSupportKHR",),
    ("vkGetPhysicalDeviceFormatProperties",),
    ("vkEnumerateDeviceExtensionProperties",),
    ("vkGetPhysicalDeviceImageFormatProperties2",),
    (),
    ("vkCreateDisplayPlaneSurfaceKHR", "VK_USE_PLATFORM_DISPLAY_KHR"),
    ("vkGetDisplayPlaneCapabilitiesKHR", "VK_USE_PLATFORM_DISPLAY_KHR"),
    ("vkGetPhysicalDeviceDisplayPropertiesKHR", "VK_USE_PLATFORM_DISPLAY_KHR"),
    ("vkGetPhysicalDeviceDisplayPlanePropertiesKHR", "VK_USE_PLATFORM_DISPLAY_KHR"),
    ("vkGetDisplayModePropertiesKHR", "VK_USE_PLATFORM_DISPLAY_KHR"),
    ("vkReleaseDisplayEXT", "VK_USE_PLATFORM_DISPLAY_KHR"),
    (),
    ("vkCreateXcbSurfaceKHR", "VK_USE_PLATFORM_XCB_KHR"),
    (),
    ("vkCreateWaylandSurfaceKHR", "VK_USE_PLATFORM_WAYLAND_KHR"),
    (),
    (
        "vkAcquireDrmDisplayEXT",
        "VK_USE_PLATFORM_WAYLAND_KHR",
        "VK_EXT_acquire_drm_display",
    ),
    ("vkGetDrmDisplayEXT", "VK_USE_PLATFORM_WAYLAND_KHR", "VK_EXT_acquire_drm_display"),
    (),
    ("vkGetRandROutputDisplayEXT", "VK_USE_PLATFORM_XLIB_XRANDR_EXT"),
    ("vkAcquireXlibDisplayEXT", "VK_USE_PLATFORM_XLIB_XRANDR_EXT"),
    (),
    ("vkCreateAndroidSurfaceKHR", "VK_USE_PLATFORM_ANDROID_KHR"),
    (),
    ("vkCreateWin32SurfaceKHR", "VK_USE_PLATFORM_WIN32_KHR"),
]

EXTENSIONS_TO_CHECK = [
    "VK_GOOGLE_display_timing",
    "VK_EXT_global_priority",
    "VK_EXT_robustness2",
]

ROOT = Path(__file__).resolve().parent.parent
DIR = ROOT / "src" / "xrt" / "auxiliary" / "vk"
HEADER_FN = DIR / "vk_helpers.h"
IMPL_FN = DIR / "vk_helpers.c"

BEGIN_TEMPLATE = "\t// beginning of GENERATED %s code - do not modify - used by scripts"
END_TEMPLATE = "\t// end of GENERATED %s code - do not modify - used by scripts"


def wrap_condition(condition):
    if "defined" in condition:
        return condition
    return "defined({})".format(condition)


def compute_condition(pp_conditions):
    if not pp_conditions:
        return None
    return " && ".join(wrap_condition(x) for x in pp_conditions)


class ConditionalGenerator:
    """Keep track of conditions to avoid unneeded repetition of ifdefs."""

    def __init__(self):
        self.current_condition = None

    def process_condition(self, new_condition: Optional[str]) -> Optional[str]:
        """Return a line (or lines) to yield if required based on the new condition state."""
        lines = []
        if self.current_condition and new_condition != self.current_condition:
            # Close current condition if required.
            lines.append("#endif  // {}".format(self.current_condition))
            # empty line
            lines.append("")
            self.current_condition = None

        if new_condition != self.current_condition:
            # Open new condition if required
            lines.append("#if {}".format(new_condition))
            self.current_condition = new_condition

        if lines:
            return "\n".join(lines)

    def finish(self) -> Optional[str]:
        """Return a line (or lines) to yield if required at the end of the loop."""
        return self.process_condition(None)


def generate_per_function(functions: List[Tuple[str, ...]], per_function_handler):
    conditional = ConditionalGenerator()
    for data in functions:
        if not data:
            # empty line
            yield ""
            continue
        condition_line = conditional.process_condition(compute_condition(data[1:]))
        if condition_line:
            yield condition_line

        yield per_function_handler(data[0])

    # close any trailing conditions
    condition_line = conditional.finish()
    if condition_line:
        yield condition_line


def generate_structure_members(functions: List[Tuple[str, ...]]):
    def per_function(name):
        return "\tPFN_{} {};".format(name, name)

    return generate_per_function(functions, per_function)


def generate_ins_proc(functions: List[Tuple[str, ...]]):
    def per_function(func: str) -> str:
        return "\tvk->{} = GET_INS_PROC(vk, {});".format(func, func)

    return generate_per_function(functions, per_function)


def generate_dev_proc(functions: List[Tuple[str, ...]]):
    def per_function(func: str) -> str:
        return "\tvk->{} = GET_DEV_PROC(vk, {});".format(func, func)

    return generate_per_function(functions, per_function)


def make_ext_member_name(ext: str):
    return "has_{}".format(ext[3:])


def make_ext_name_define(ext: str):
    return "{}_EXTENSION_NAME".format(ext.upper()).replace("2", "_2")


def generate_ext_members():
    for ext in EXTENSIONS_TO_CHECK:
        yield "\tbool {};".format(make_ext_member_name(ext))


def generate_ext_check():
    yield "\t// Reset before filling out."

    for ext in EXTENSIONS_TO_CHECK:
        yield "\tvk->{} = false;".format(make_ext_member_name(ext))

    yield ""
    yield "\tfor (uint32_t i = 0; i < device_extension_count; i++) {"
    yield "\t\tconst char *ext = device_extensions[i];"
    yield ""

    conditional = ConditionalGenerator()
    for ext in EXTENSIONS_TO_CHECK:
        condition_line = conditional.process_condition(compute_condition((ext,)))
        if condition_line:
            yield condition_line
        yield "\t\tif (strcmp(ext, {}) == 0) {{".format(make_ext_name_define(ext))
        yield "\t\t\tvk->{} = true;".format(make_ext_member_name(ext))
        yield "\t\t\tcontinue;"
        yield "\t\t}"
    # close any trailing conditions
    condition_line = conditional.finish()
    if condition_line:
        yield condition_line
    yield "\t}"


def replace_middle(
    lines: List[str], start_sentinel: str, end_sentinel: str, new_middle: List[str]
) -> List[str]:
    middle_start = lines.index(start_sentinel) + 1
    middle_end = lines.index(end_sentinel)
    return lines[:middle_start] + new_middle + lines[middle_end:]


DEVICE_TEMPLATES = {
    "BEGIN": BEGIN_TEMPLATE % "device loader",
    "END": END_TEMPLATE % "device loader",
}
INSTANCE_TEMPLATES = {
    "BEGIN": BEGIN_TEMPLATE % "instance loader",
    "END": END_TEMPLATE % "instance loader",
}
EXT_TEMPLATES = {
    "BEGIN": BEGIN_TEMPLATE % "extension",
    "END": END_TEMPLATE % "extension",
}


def process_header():
    with open(str(HEADER_FN), "r", encoding="utf-8") as fp:
        lines = [line.rstrip() for line in fp.readlines()]

    lines = replace_middle(
        lines,
        INSTANCE_TEMPLATES["BEGIN"],
        INSTANCE_TEMPLATES["END"],
        list(generate_structure_members(INSTANCE_FUNCTIONS)),
    )

    lines = replace_middle(
        lines,
        DEVICE_TEMPLATES["BEGIN"],
        DEVICE_TEMPLATES["END"],
        list(generate_structure_members(DEVICE_FUNCTIONS)),
    )
    lines = replace_middle(
        lines,
        EXT_TEMPLATES["BEGIN"],
        EXT_TEMPLATES["END"],
        list(generate_ext_members()),
    )

    with open(str(HEADER_FN), "w", encoding="utf-8") as fp:
        fp.write("\n".join(lines))
        fp.write("\n")


def process_impl():
    with open(str(IMPL_FN), "r", encoding="utf-8") as fp:
        lines = [line.rstrip() for line in fp.readlines()]

    lines = replace_middle(
        lines,
        INSTANCE_TEMPLATES["BEGIN"],
        INSTANCE_TEMPLATES["END"],
        list(generate_ins_proc(INSTANCE_FUNCTIONS)),
    )

    lines = replace_middle(
        lines,
        DEVICE_TEMPLATES["BEGIN"],
        DEVICE_TEMPLATES["END"],
        list(generate_dev_proc(DEVICE_FUNCTIONS)),
    )

    lines = replace_middle(
        lines,
        EXT_TEMPLATES["BEGIN"],
        EXT_TEMPLATES["END"],
        list(generate_ext_check()),
    )

    with open(str(IMPL_FN), "w", encoding="utf-8") as fp:
        fp.write("\n".join(lines))
        fp.write("\n")


if __name__ == "__main__":
    process_header()
    process_impl()
