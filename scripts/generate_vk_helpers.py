#!/usr/bin/env python3
# Copyright 2019-2021, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0
"""Simple script to update vk_helpers.{c,h}."""

from pathlib import Path
from typing import Callable, Iterable, List, Optional


def get_device_cmds():
    return [
        Cmd("vkDestroyDevice"),
        Cmd("vkDeviceWaitIdle"),
        Cmd("vkAllocateMemory"),
        Cmd("vkFreeMemory"),
        Cmd("vkMapMemory"),
        Cmd("vkUnmapMemory"),
        None,
        Cmd("vkCreateBuffer"),
        Cmd("vkDestroyBuffer"),
        Cmd("vkBindBufferMemory"),
        None,
        Cmd("vkCreateImage"),
        Cmd("vkDestroyImage"),
        Cmd("vkBindImageMemory"),
        None,
        Cmd("vkGetBufferMemoryRequirements"),
        Cmd("vkFlushMappedMemoryRanges"),
        Cmd("vkGetImageMemoryRequirements"),
        Cmd(
            "vkGetImageMemoryRequirements2KHR",
            member_name="vkGetImageMemoryRequirements2",
        ),
        Cmd("vkGetImageSubresourceLayout"),
        None,
        Cmd("vkCreateImageView"),
        Cmd("vkDestroyImageView"),
        None,
        Cmd("vkCreateSampler"),
        Cmd("vkDestroySampler"),
        None,
        Cmd("vkCreateShaderModule"),
        Cmd("vkDestroyShaderModule"),
        None,
        Cmd("vkCreateQueryPool"),
        Cmd("vkDestroyQueryPool"),
        Cmd("vkGetQueryPoolResults"),
        None,
        Cmd("vkCreateCommandPool"),
        Cmd("vkDestroyCommandPool"),
        Cmd("vkResetCommandPool"),
        None,
        Cmd("vkAllocateCommandBuffers"),
        Cmd("vkBeginCommandBuffer"),
        Cmd("vkCmdBeginQuery"),
        Cmd("vkCmdCopyQueryPoolResults"),
        Cmd("vkCmdEndQuery"),
        Cmd("vkCmdResetQueryPool"),
        Cmd("vkCmdWriteTimestamp"),
        Cmd("vkCmdPipelineBarrier"),
        Cmd("vkCmdBeginRenderPass"),
        Cmd("vkCmdSetScissor"),
        Cmd("vkCmdSetViewport"),
        Cmd("vkCmdClearColorImage"),
        Cmd("vkCmdEndRenderPass"),
        Cmd("vkCmdBindDescriptorSets"),
        Cmd("vkCmdBindPipeline"),
        Cmd("vkCmdBindVertexBuffers"),
        Cmd("vkCmdBindIndexBuffer"),
        Cmd("vkCmdDraw"),
        Cmd("vkCmdDrawIndexed"),
        Cmd("vkCmdDispatch"),
        Cmd("vkCmdCopyBuffer"),
        Cmd("vkCmdCopyBufferToImage"),
        Cmd("vkCmdCopyImage"),
        Cmd("vkCmdCopyImageToBuffer"),
        Cmd("vkCmdBlitImage"),
        Cmd("vkEndCommandBuffer"),
        Cmd("vkFreeCommandBuffers"),
        None,
        Cmd("vkCreateRenderPass"),
        Cmd("vkDestroyRenderPass"),
        None,
        Cmd("vkCreateFramebuffer"),
        Cmd("vkDestroyFramebuffer"),
        None,
        Cmd("vkCreatePipelineCache"),
        Cmd("vkDestroyPipelineCache"),
        None,
        Cmd("vkResetDescriptorPool"),
        Cmd("vkCreateDescriptorPool"),
        Cmd("vkDestroyDescriptorPool"),
        None,
        Cmd("vkAllocateDescriptorSets"),
        Cmd("vkFreeDescriptorSets"),
        None,
        Cmd("vkCreateComputePipelines"),
        Cmd("vkCreateGraphicsPipelines"),
        Cmd("vkDestroyPipeline"),
        None,
        Cmd("vkCreatePipelineLayout"),
        Cmd("vkDestroyPipelineLayout"),
        None,
        Cmd("vkCreateDescriptorSetLayout"),
        Cmd("vkUpdateDescriptorSets"),
        Cmd("vkDestroyDescriptorSetLayout"),
        None,
        Cmd("vkGetDeviceQueue"),
        Cmd("vkQueueSubmit"),
        Cmd("vkQueueWaitIdle"),
        None,
        Cmd("vkCreateSemaphore"),
        Cmd(
            "vkSignalSemaphoreKHR",
            member_name="vkSignalSemaphore",
            requires=("VK_KHR_timeline_semaphore",),
        ),
        Cmd(
            "vkWaitSemaphoresKHR",
            member_name="vkWaitSemaphores",
            requires=("VK_KHR_timeline_semaphore",),
        ),
        Cmd(
            "vkGetSemaphoreCounterValueKHR",
            member_name="vkGetSemaphoreCounterValue",
            requires=("VK_KHR_timeline_semaphore",),
        ),
        Cmd("vkDestroySemaphore"),
        None,
        Cmd("vkCreateFence"),
        Cmd("vkWaitForFences"),
        Cmd("vkGetFenceStatus"),
        Cmd("vkDestroyFence"),
        Cmd("vkResetFences"),
        None,
        Cmd("vkCreateSwapchainKHR"),
        Cmd("vkDestroySwapchainKHR"),
        Cmd("vkGetSwapchainImagesKHR"),
        Cmd("vkAcquireNextImageKHR"),
        Cmd("vkQueuePresentKHR"),
        None,
        Cmd("vkGetMemoryWin32HandleKHR", requires=("VK_USE_PLATFORM_WIN32_KHR",)),
        Cmd("vkGetFenceWin32HandleKHR", requires=("VK_USE_PLATFORM_WIN32_KHR",)),
        Cmd("vkGetSemaphoreWin32HandleKHR", requires=("VK_USE_PLATFORM_WIN32_KHR",)),
        Cmd("vkImportFenceWin32HandleKHR", requires=("VK_USE_PLATFORM_WIN32_KHR",)),
        Cmd("vkImportSemaphoreWin32HandleKHR", requires=("VK_USE_PLATFORM_WIN32_KHR",)),
        None,
        Cmd("vkGetMemoryFdKHR", requires=("!defined(VK_USE_PLATFORM_WIN32_KHR)",)),
        Cmd("vkGetFenceFdKHR", requires=("!defined(VK_USE_PLATFORM_WIN32_KHR)",)),
        Cmd("vkGetSemaphoreFdKHR", requires=("!defined(VK_USE_PLATFORM_WIN32_KHR)",)),
        Cmd("vkImportFenceFdKHR", requires=("!defined(VK_USE_PLATFORM_WIN32_KHR)",)),
        Cmd("vkImportSemaphoreFdKHR", requires=("!defined(VK_USE_PLATFORM_WIN32_KHR)",)),
        None,
        Cmd(
            "vkGetMemoryAndroidHardwareBufferANDROID",
            requires=("VK_USE_PLATFORM_ANDROID_KHR",),
        ),
        Cmd(
            "vkGetAndroidHardwareBufferPropertiesANDROID",
            requires=("VK_USE_PLATFORM_ANDROID_KHR",),
        ),
        None,
        Cmd("vkGetCalibratedTimestampsEXT", requires=("VK_EXT_calibrated_timestamps",)),
        None,
        Cmd("vkGetPastPresentationTimingGOOGLE"),
        None,
        Cmd("vkGetSwapchainCounterEXT", requires=("VK_EXT_display_control",)),
        Cmd("vkRegisterDeviceEventEXT", requires=("VK_EXT_display_control",)),
        Cmd("vkRegisterDisplayEventEXT", requires=("VK_EXT_display_control",)),
    ]


def get_instance_cmds():
    return [
        Cmd("vkDestroyInstance"),
        Cmd("vkGetDeviceProcAddr"),
        Cmd("vkCreateDevice"),
        Cmd("vkDestroySurfaceKHR"),
        None,
        Cmd("vkCreateDebugReportCallbackEXT"),
        Cmd("vkDestroyDebugReportCallbackEXT"),
        None,
        Cmd("vkEnumeratePhysicalDevices"),
        Cmd("vkGetPhysicalDeviceProperties"),
        Cmd("vkGetPhysicalDeviceProperties2"),
        Cmd("vkGetPhysicalDeviceFeatures2"),
        Cmd("vkGetPhysicalDeviceMemoryProperties"),
        Cmd("vkGetPhysicalDeviceQueueFamilyProperties"),
        Cmd("vkGetPhysicalDeviceSurfaceCapabilitiesKHR"),
        Cmd("vkGetPhysicalDeviceSurfaceFormatsKHR"),
        Cmd("vkGetPhysicalDeviceSurfacePresentModesKHR"),
        Cmd("vkGetPhysicalDeviceSurfaceSupportKHR"),
        Cmd("vkGetPhysicalDeviceFormatProperties"),
        Cmd("vkGetPhysicalDeviceImageFormatProperties2"),
        Cmd("vkGetPhysicalDeviceExternalBufferPropertiesKHR"),
        Cmd("vkGetPhysicalDeviceExternalFencePropertiesKHR"),
        Cmd("vkGetPhysicalDeviceExternalSemaphorePropertiesKHR"),
        Cmd("vkEnumerateDeviceExtensionProperties"),
        Cmd("vkEnumerateDeviceLayerProperties"),
        None,
        Cmd(
            "vkGetPhysicalDeviceCalibrateableTimeDomainsEXT",
            requires=("VK_EXT_calibrated_timestamps",),
        ),
        None,
        Cmd(
            "vkCreateDisplayPlaneSurfaceKHR", requires=("VK_USE_PLATFORM_DISPLAY_KHR",)
        ),
        Cmd(
            "vkGetDisplayPlaneCapabilitiesKHR",
            requires=("VK_USE_PLATFORM_DISPLAY_KHR",),
        ),
        Cmd(
            "vkGetPhysicalDeviceDisplayPropertiesKHR",
            requires=("VK_USE_PLATFORM_DISPLAY_KHR",),
        ),
        Cmd(
            "vkGetPhysicalDeviceDisplayPlanePropertiesKHR",
            requires=("VK_USE_PLATFORM_DISPLAY_KHR",),
        ),
        Cmd("vkGetDisplayModePropertiesKHR", requires=("VK_USE_PLATFORM_DISPLAY_KHR",)),
        Cmd("vkReleaseDisplayEXT", requires=("VK_USE_PLATFORM_DISPLAY_KHR",)),
        None,
        Cmd("vkCreateXcbSurfaceKHR", requires=("VK_USE_PLATFORM_XCB_KHR",)),
        None,
        Cmd("vkCreateWaylandSurfaceKHR", requires=("VK_USE_PLATFORM_WAYLAND_KHR",)),
        None,
        Cmd(
            "vkAcquireDrmDisplayEXT",
            requires=("VK_USE_PLATFORM_WAYLAND_KHR", "VK_EXT_acquire_drm_display"),
        ),
        Cmd(
            "vkGetDrmDisplayEXT",
            requires=("VK_USE_PLATFORM_WAYLAND_KHR", "VK_EXT_acquire_drm_display"),
        ),
        None,
        Cmd(
            "vkGetRandROutputDisplayEXT", requires=("VK_USE_PLATFORM_XLIB_XRANDR_EXT",)
        ),
        Cmd("vkAcquireXlibDisplayEXT", requires=("VK_USE_PLATFORM_XLIB_XRANDR_EXT",)),
        None,
        Cmd("vkCreateAndroidSurfaceKHR", requires=("VK_USE_PLATFORM_ANDROID_KHR",)),
        None,
        Cmd("vkCreateWin32SurfaceKHR", requires=("VK_USE_PLATFORM_WIN32_KHR",)),
        None,
        Cmd("vkGetPhysicalDeviceSurfaceCapabilities2EXT", requires=("VK_EXT_display_surface_counter",))
    ]


# Sorted KHR, EXT, Vendor, interally alphabetically
INSTANCE_EXTENSIONS_TO_CHECK = [
    "VK_EXT_display_surface_counter",
]
# Sorted KHR, EXT, Vendor, interally alphabetically
DEVICE_EXTENSIONS_TO_CHECK = [
    "VK_KHR_external_fence_fd",
    "VK_KHR_external_semaphore_fd",
    "VK_KHR_image_format_list",
    "VK_KHR_maintenance1",
    "VK_KHR_maintenance2",
    "VK_KHR_maintenance3",
    "VK_KHR_maintenance4",
    "VK_KHR_timeline_semaphore",
    "VK_EXT_calibrated_timestamps",
    "VK_EXT_display_control",
    "VK_EXT_global_priority",
    "VK_EXT_robustness2",
    "VK_GOOGLE_display_timing",
]

ROOT = Path(__file__).resolve().parent.parent
DIR = ROOT / "src" / "xrt" / "auxiliary" / "vk"
HELPERS_H_FN = DIR / "vk_helpers.h"
BUNDLE_INIT_C_FN = DIR / "vk_bundle_init.c"
FUNCTION_LOADERS_C_FN = DIR / "vk_function_loaders.c"

BEGIN_TEMPLATE = "\t// beginning of GENERATED %s code - do not modify - used by scripts"
END_TEMPLATE = "\t// end of GENERATED %s code - do not modify - used by scripts"


class Cmd:
    def __init__(
        self,
        name: str,
        member_name: Optional[str] = None,
        *,
        requires: Optional[Iterable[str]] = None,
    ):
        self.name = name
        if not member_name:
            member_name = name
        self.member_name = member_name
        if not requires:
            # normalize empty lists to None
            requires = None
        self.requires = requires

    def __repr__(self) -> str:
        args = [repr(self.name)]
        if self.member_name != self.name:
            args.append(repr(self.member_name))
        if self.requires:
            args.append(f"requires={repr(self.requires)}")
        return "Function({})".format(", ".join(args))


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
            lines.append("#endif // {}".format(self.current_condition))
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


def generate_per_command(
    commands: List[Cmd], per_command_handler: Callable[[Cmd], str]
):
    conditional = ConditionalGenerator()
    for cmd in commands:
        if not cmd:
            # empty line
            yield ""
            continue
        condition = compute_condition(cmd.requires)
        condition_line = conditional.process_condition(condition)
        if condition_line:
            yield condition_line

        yield per_command_handler(cmd)

    # close any trailing conditions
    condition_line = conditional.finish()
    if condition_line:
        yield condition_line


def generate_structure_members(commands: List[Cmd]):
    def per_command(cmd: Cmd):
        return "\tPFN_{} {};".format(cmd.name, cmd.member_name)

    return generate_per_command(commands, per_command)


def generate_proc_macro(macro: str, commands: List[Cmd]):
    name_width = max([len(cmd.member_name) for cmd in commands if cmd])

    def per_command(cmd: Cmd) -> str:
        return "\tvk->{} = {}(vk, {});".format(
            cmd.member_name.ljust(name_width), macro, cmd.name
        )

    return generate_per_command(
        commands,
        per_command,
    )


def make_ext_member_name(ext: str):
    return "has_{}".format(ext[3:])


def make_ext_name_define(ext: str):
    str = ext.upper()
    str = str.replace("1", "_1")
    str = str.replace("2", "_2")
    str = str.replace("3", "_3")
    str = str.replace("4", "_4")

    return "{}_EXTENSION_NAME".format(str)


def generate_ext_members(exts):
    for ext in exts:
        yield "\tbool {};".format(make_ext_member_name(ext))


def generate_ext_check(exts):
    yield "\t// Reset before filling out."

    for ext in exts:
        yield "\tvk->{} = false;".format(make_ext_member_name(ext))

    yield ""
    yield "\tconst char *const *exts = u_string_list_get_data(ext_list);"
    yield "\tuint32_t ext_count = u_string_list_get_size(ext_list);"
    yield ""
    yield "\tfor (uint32_t i = 0; i < ext_count; i++) {"
    yield "\t\tconst char *ext = exts[i];"
    yield ""

    conditional = ConditionalGenerator()
    for ext in exts:
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
INSTANCE_EXT_TEMPLATES = {
    "BEGIN": BEGIN_TEMPLATE % "instance extension",
    "END": END_TEMPLATE % "instance extension",
}
EXT_TEMPLATES = {
    "BEGIN": BEGIN_TEMPLATE % "device extension",
    "END": END_TEMPLATE % "device extension",
}


def process_helpers_h():
    with open(str(HELPERS_H_FN), "r", encoding="utf-8") as fp:
        lines = [line.rstrip() for line in fp.readlines()]

    lines = replace_middle(
        lines,
        INSTANCE_TEMPLATES["BEGIN"],
        INSTANCE_TEMPLATES["END"],
        list(generate_structure_members(get_instance_cmds())),
    )

    lines = replace_middle(
        lines,
        DEVICE_TEMPLATES["BEGIN"],
        DEVICE_TEMPLATES["END"],
        list(generate_structure_members(get_device_cmds())),
    )

    lines = replace_middle(
        lines,
        INSTANCE_EXT_TEMPLATES["BEGIN"],
        INSTANCE_EXT_TEMPLATES["END"],
        list(generate_ext_members(INSTANCE_EXTENSIONS_TO_CHECK)),
    )

    lines = replace_middle(
        lines,
        EXT_TEMPLATES["BEGIN"],
        EXT_TEMPLATES["END"],
        list(generate_ext_members(DEVICE_EXTENSIONS_TO_CHECK)),
    )

    with open(str(HELPERS_H_FN), "w", encoding="utf-8") as fp:
        fp.write("\n".join(lines))
        fp.write("\n")


def process_function_loaders_c():
    with open(str(FUNCTION_LOADERS_C_FN), "r", encoding="utf-8") as fp:
        lines = [line.rstrip() for line in fp.readlines()]

    lines = replace_middle(
        lines,
        INSTANCE_TEMPLATES["BEGIN"],
        INSTANCE_TEMPLATES["END"],
        list(generate_proc_macro("GET_INS_PROC", get_instance_cmds())),
    )

    lines = replace_middle(
        lines,
        DEVICE_TEMPLATES["BEGIN"],
        DEVICE_TEMPLATES["END"],
        list(generate_proc_macro("GET_DEV_PROC", get_device_cmds())),
    )

    with open(str(FUNCTION_LOADERS_C_FN), "w", encoding="utf-8") as fp:
        fp.write("\n".join(lines))
        fp.write("\n")

def process_bundle_init_c():
    with open(str(BUNDLE_INIT_C_FN), "r", encoding="utf-8") as fp:
        lines = [line.rstrip() for line in fp.readlines()]


    lines = replace_middle(
        lines,
        INSTANCE_EXT_TEMPLATES["BEGIN"],
        INSTANCE_EXT_TEMPLATES["END"],
        list(generate_ext_check(INSTANCE_EXTENSIONS_TO_CHECK)),
    )

    lines = replace_middle(
        lines,
        EXT_TEMPLATES["BEGIN"],
        EXT_TEMPLATES["END"],
        list(generate_ext_check(DEVICE_EXTENSIONS_TO_CHECK)),
    )

    with open(str(BUNDLE_INIT_C_FN), "w", encoding="utf-8") as fp:
        fp.write("\n".join(lines))
        fp.write("\n")


if __name__ == "__main__":
    process_helpers_h()
    process_bundle_init_c()
    process_function_loaders_c()
