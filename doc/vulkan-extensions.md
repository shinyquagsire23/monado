# Vulkan extensions used by Monado {#vulkan-extensions}

<!--
Copyright 2020, Collabora, Ltd. and the Monado contributors
SPDX-License-Identifier: BSL-1.0
-->

<!--
adjacent vertical lines: for column spans. Aligning final vertical line
with last column's closing bar, to keep the text looking close to the rendered
version.

Using manual "footnotes" to keep table somewhat narrow.

Do not reflow this table!

Edit with word-wrap disabled and with a multiple-cursor capable editor to
minimize frustration.

-->

|                                                    | Client | XCB server (a) | Wayland server (a) | Xlib-xrandr server (b) | NVIDIA xrandr server (b) | Android server | Windows server |
| ---------------------------------------------------|--------|----------------|--------------------|------------------------|--------------------------|----------------|----------------|
| **Instance extensions**                                                                                                                                                          ||||||||
| [`VK_KHR_external_fence_capabilities`][] (8)       | yes?                                                                                                                         |||||||
| [`VK_KHR_external_memory_capabilities`][] (8)      | yes?                                                                                                                         |||||||
| [`VK_KHR_external_semaphore_capabilities`][] (8)   | yes?                                                                                                                         |||||||
| [`VK_KHR_get_physical_device_properties2`][] (8)   | yes                                                                                                                          |||||||
| [`VK_KHR_surface`][]                               |        | yes                                                                                                                  ||||||
| [`VK_KHR_display`][]                               |        |                |                    | yes (2) (requires `VK_KHR_surface`)              ||                |                |
| **Platform-specific instance extensions**                                                                                                                                        ||||||||
| [`VK_KHR_xcb_surface`][]                           |        | yes (1, 4)     |                    |                        |                          |                |                |
| [`VK_KHR_wayland_surface`][]                       |        |                | yes (1, 4)         |                        |                          |                |                |
| [`VK_EXT_direct_mode_display`][]                   |        |                |                    | yes (1)                | yes (2)                  |                |                |
| [`VK_EXT_acquire_xlib_display`][]                  |        |                |                    | yes (1) (in shared code)                         ||                |                |
| [`VK_KHR_android_surface`][]                       |        |                |                    |                        |                          | yes (1, 4)     |                |
| [`VK_KHR_win32_surface`][]                         |        |                |                    |                        |                          |                | yes (1, 4)     |
| **Device Extensions**                                                                                                                                                            ||||||||
| [`VK_KHR_get_memory_requirements2`][] (8)          | yes                                                                                                                          |||||||
| [`VK_KHR_dedicated_allocation`][] (8)              | yes? (requires `VK_KHR_get_memory_requirements2`)                                                                            |||||||
| [`VK_KHR_external_fence`][] (8) (+platform: 5)     | yes (soon)                                                                                                                   |||||||
| [`VK_KHR_external_memory`][] (8) (+platform: 6)    | yes                                                                                                                          |||||||
| [`VK_KHR_external_semaphore`][] (8) (+platform: 7) | yes (soon)                                                                                                                   |||||||
| [`VK_KHR_swapchain`][]                             |        | yes                                                                                                                  ||||||

## Notes

Kept out of the table above to limit its width.

* Server type:
  * a: Windowed
  * b: Direct mode
* Usage details/reason:
  * 1: Used directly
  * 2: Dependency of `VK_EXT_direct_mode_display`
  * 3: Dependency of `VK_EXT_acquire_xlib_display`
  * 4: Platform extension building on `VK_KHR_surface`
  * 5: Platform-specific extensions building on `VK_KHR_external_fence`:
    * Linux and Android: [`VK_KHR_external_fence_fd`][]
    * Windows: [`VK_KHR_external_fence_win32`][]
    * Note: These platform-specific extensions were not promoted to Core in
      Vulkan 1.1, only the platform-independent base extension.
  * 6: Platform-specific extensions building on `VK_KHR_external_memory`:
    * Linux: [`VK_KHR_external_memory_fd`][]
    * Android: [`VK_ANDROID_external_memory_android_hardware_buffer`][] (`fd`
      also usually available?)
    * Windows: [`VK_KHR_external_memory_win32`][]
    * Note: These platform-specific extensions were not promoted to Core in
      Vulkan 1.1, only the platform-independent base extension.
  * 7: Platform-specific extensions building on `VK_KHR_external_semaphore`:
    * Linux and Android: [`VK_KHR_external_semaphore_fd`][]
    * Windows: [`VK_KHR_external_semaphore_win32`][]
    * Note: These platform-specific extensions were not promoted to Core in
      Vulkan 1.1, only the platform-independent base extension.
  * 8: Promoted to Vulkan 1.1 Core

<!-- links to the extension references, out of line to keep the table source readable -->
<!-- They don't show up like this in the formatted document. -->

[`VK_KHR_external_fence_capabilities`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_external_fence_capabilities.html
[`VK_KHR_external_memory_capabilities`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_external_memory_capabilities.html
[`VK_KHR_external_semaphore_capabilities`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_external_semaphore_capabilities.html
[`VK_KHR_get_physical_device_properties2`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_get_physical_device_properties2.html
[`VK_KHR_surface`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_surface.html
[`VK_KHR_display`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_display.html
[`VK_KHR_xcb_surface`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_xcb_surface.html
[`VK_KHR_wayland_surface`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_wayland_surface.html
[`VK_EXT_direct_mode_display`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_EXT_direct_mode_display.html
[`VK_EXT_acquire_xlib_display`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_EXT_acquire_xlib_display.html
[`VK_KHR_android_surface`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_android_surface.html
[`VK_KHR_win32_surface`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_win32_surface.html
[`VK_KHR_dedicated_allocation`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_dedicated_allocation.html
[`VK_KHR_external_fence`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_external_fence.html
[`VK_KHR_external_memory`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_external_memory.html
[`VK_KHR_external_semaphore`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_external_semaphore.html
[`VK_KHR_get_memory_requirements2`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_get_memory_requirements2.html
[`VK_KHR_swapchain`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_swapchain.html
[`VK_KHR_external_fence_fd`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_external_fence_fd.html
[`VK_KHR_external_fence_win32`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_external_fence_win32.html
[`VK_KHR_external_memory_fd`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_external_memory_fd.html
[`VK_ANDROID_external_memory_android_hardware_buffer`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_ANDROID_external_memory_android_hardware_buffer.html
[`VK_KHR_external_memory_win32`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_external_memory_win32.html
[`VK_KHR_external_semaphore_fd`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_external_semaphore_fd.html
[`VK_KHR_external_semaphore_win32`]: https://khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_external_semaphore_win32.html

## Reasons

* Instance extensions:
  * `VK_KHR_surface` - for configuring output surface.
  * `VK_KHR_get_physical_device_properties2` - for getting device UUID to share
    between client compositor and main/native compositor.

* Device extensions:
  * `VK_KHR_swapchain` - for displaying output on a display output.

## Code locations

* Client
  * Instance extensions:
    [xrt_gfx_vk_instance_extensions](@ref xrt_gfx_vk_instance_extensions)
  * Device extensions:
    [xrt_gfx_vk_device_extensions](@ref xrt_gfx_vk_device_extensions)

* Server
  * All these are in [comp_compositor.c](@ref comp_compositor.c), with the
    extensions required by all servers defined in
    `COMP_INSTANCE_EXTENSIONS_COMMON`
  * XCB (Windowed) Server: `instance_extensions_xcb`
  * Wayland (Windowed) Server: `instance_extensions_wayland`
  * Xlib-xrandr direct mode server and NVIDIA direct mode server:
    `instance_extensions_direct_mode`
  * Android server: `instance_extensions_android`
  * Windows server: `instance_extensions_windows`
