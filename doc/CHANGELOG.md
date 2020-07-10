# Changelog for Monado

```txt
SPDX-License-Identifier: CC0-1.0
SPDX-FileCopyrightText: 2020 Collabora, Ltd. and the Monado contributors
```

## Monado 0.3.0 (2020-07-10)

- Major changes
  - Centralise the logging functionality in Monado to a single util helper.
    Previously most of our logging was done via fprints and gated behind booleans,
    now there are common functions to call and a predfined set of levels.
    ([!408](https://gitlab.freedesktop.org/monado/monado/merge_requests/408),
    [!409](https://gitlab.freedesktop.org/monado/monado/merge_requests/409))
- XRT Interface
  - compositor: Remove the `array_size` field from the struct, this was the only
    state tracker supplied value that was on the struct, only have values that the
    compositor decides over on the struct.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - Improve Doxygen documentation of interfaces. Now the inheritance structure and
    implementation of interfaces is shown in the docs, and helper functions that
    call through function pointers are listed as "member functions", to help
    developers understand the internal structure of Monado better.
    ([!365](https://gitlab.freedesktop.org/monado/monado/merge_requests/365),
    [!367](https://gitlab.freedesktop.org/monado/monado/merge_requests/367))
  - xrt: Add xrt_result_t return type to many compositor functions that previously
    had no way to indicate failure.
    ([!369](https://gitlab.freedesktop.org/monado/monado/merge_requests/369))
  - compositor: Introduce `xrt_swapchain_create_info` simplifying the argument
    passing between various layers of the compositor stack and also simplify future
    refactoring projects.
    ([!407](https://gitlab.freedesktop.org/monado/monado/merge_requests/407))
- State Trackers
  - OpenXR: Update headers to 1.0.9.
    ([!358](https://gitlab.freedesktop.org/monado/monado/merge_requests/358))
  - OpenXR: Verify that the XrViewConfigurationType is supported by the system as
    required by the OpenXR spec in xrEnumerateEnvironmentBlendModes.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - OpenXR: Return the correct error code when verifying the sub action, if it is
    a
    valid sub action path but not given at action creation we should return
    `XR_ERROR_PATH_UNSUPPORTED`.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - OpenXR: Validate the subImage data for both projection and quad layers layers,
    refactor code out so it can be shared with the different types of layers. Need
    to track some state on the `oxr_swapchain` in order to do the checking.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - OpenXR: Correct the return error code for action and action set localized name
    validation.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - OpenXR: Correct the error messages on sub action paths errors.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - OpenXR: Track the name and localized name for both actions and action sets,
    that
    way we can make sure that there are no duplicates. This is required by the
    spec. ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - OpenXR: Do better checking if action sets and actions have been attached to the
    session or not.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - OpenXR: Validate the arguments for `xrSuggestInteractionProfileBindings` better
    so that it follows the spec better.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - OpenXR: Rework the logging formatting of error messages, this makes it easier
    to
    read for the application developer.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - OpenXR: Correctly ensure that the application has called the required get
    graphics requirements function when creating a session.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - OpenXR: When a `XrSession` is destroyed purge the event queue of any events
    that
    references to it so that no events gets delivered to the applications with
    stales handles.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - OpenXR: Make the event queue thread safe, all done with a simple mutex that is
    not held for long at all.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - OpenXR: A major overhaul of the swapchain acquire, wait and release code. This
    makes it almost completely conformant with the spec. Tricky parts include that
    multiple images can be acquired, but only one can be waited on before being
    released.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - OpenXR: Enforce that static swapchains can only be acquired once, this is
    required by the spec and make sure that a image is only rendered to once, and
    allows the runtime to perform special optimizations on the image.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - OpenXR: Make the function `xrGetReferenceSpaceBoundsRect` at least conform to
    the spec without actually implementing it, currently we do not track bounds in
    Monado.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - OpenXR: Make the session state changes obey the specification. The code is
    fairly hair as is and should be improved at a later time.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - OpenXR: Use the correct XrPath for `/user/gamepad` while it sits in the users
    hand itsn't `/user/hand/gamepad` as previously believed.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - OpenXR: Where used make sure we verify the view configuration type is a valid
    enum value, the code is setup so that we in the future can support new values
    via extensions easily.
    ([!368](https://gitlab.freedesktop.org/monado/monado/merge_requests/368))
  - OpenXR: More correctly verify the interactive profile binding data, including
    the given interactive profile is correct and the binding point is valid.
    ([!377](https://gitlab.freedesktop.org/monado/monado/merge_requests/377))
  - OpenXR: Transform input types in a somewhat flexible, composable way. Also, do
    conversion at sync time, and use the transformed values to evaluate if the
    input has changed, per the spec.
    ([!379](https://gitlab.freedesktop.org/monado/monado/merge_requests/379))
  - OpenXR: Tidy the extensions generated by the script and order them according
    to
    extension prefix, starting with KHR, EXT, Vendor, KHRX, EXTX, VendorX. Also
    rename the `MND_ball_on_stick_controller` to `MNDX_ball_on_a_stick_controller`.
    ([!410](https://gitlab.freedesktop.org/monado/monado/merge_requests/410))
  - OpenXR: Fix overly attached action sets, which would appear to be attached to
    a
    session even after the session has been destroyed. Also tidy up comments and
    other logic surrounding this.
    ([!411](https://gitlab.freedesktop.org/monado/monado/merge_requests/411))
- Drivers
  - psvr: Normalize the rotation to not trip up the client app when it gives the
    rotation back to `st/oxr` again.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - vive: Create vive_config module to isolate config code and avoid duplication
    between controller and headset code.
    vive: Probe for controllers in vive_proper
    interface.
    vive: Fix a bug where using the Vive Pro crashed Monado.
    vive: Fix a
    bug where the controller didn't parse JSON vectors correctly.
    vive: Move
    missing functions to and use u_json.
    ([!405](https://gitlab.freedesktop.org/monado/monado/merge_requests/405))
  - vive: Add support for Gen1 and Gen2 Vive Trackers.
    ([!406](https://gitlab.freedesktop.org/monado/monado/merge_requests/406))
  - vive: Port to new u_logging API.
    ([!417](https://gitlab.freedesktop.org/monado/monado/merge_requests/417))
  - comp: Set a compositor window title.
    ([!418](https://gitlab.freedesktop.org/monado/monado/merge_requests/418))
- IPC
  - server: Almost completely overhaul the handling of swapchain life cycle
    including: correctly track which swapchains are alive; reuse ids; enforce the
    maximum number of swapchains; and destroy underlying swapchains when they are
    destroyed by the client.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - util: Make sure to not access NULL control messages, say in the case of the
    server failing to create a swapchain. Also add a whole bunch of paranoia when
    it comes to the alignment of the control message buffers.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - ipc: Return XR_ERROR_INSTANCE_LOST on IPC errors.
    ([!369](https://gitlab.freedesktop.org/monado/monado/merge_requests/369))
- Compositor
  - main: Include `<math.h>` in layers renderer for missing `tanf` function.
    ([!358](https://gitlab.freedesktop.org/monado/monado/merge_requests/358))
  - swapchain: Give out the oldset image index when a image is acquired. This logic
    can be made better, but will work for the good case.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - swapchain: Close any FDs that are still valid, for instance the ipc server
    copies the FDs to the client.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - client: When we give a image fd to the either OpenGL or Vulkan it is consumed
    and can not be rused. So make sure that it is set to an invalid fd value on the
    `xrt_image_fd` on the owning `xrt_swapchain_fd`.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - main: We were alpha blending all layers previously, but now we support the
    layer flag that OpenXR gives us. We do this by using different `VkImageView`s
    with different component swizzles.
    ([!394](https://gitlab.freedesktop.org/monado/monado/merge_requests/394))
  - layer_rendering: Use the visibility flags on quad to correctly show the layers
    in each eye.
    ([!394](https://gitlab.freedesktop.org/monado/monado/merge_requests/394))
- Helper Libraries
  - os/threading: Include `xrt_compiler.h` to fix missing stdint types.
    ([!358](https://gitlab.freedesktop.org/monado/monado/merge_requests/358))
  - util: Add a very simple fifo for indices, this is used to keep track of
    swapchain in order of age (oldness).
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
  - util: Expand `u_hashset` to be able to automatically allocate a `u_hashet_item`
    and insert it.
    ([!359](https://gitlab.freedesktop.org/monado/monado/merge_requests/359))
- Misc. Features
  - build: Allow enabling inter-procedural optimization in CMake GUIs, if supported
    by platform and compiler.
    ([!330](https://gitlab.freedesktop.org/monado/monado/merge_requests/330))
- Misc. Fixes
  - No significant changes

## Monado 0.2 (2020-05-29)

- Major changes
  - Add support for a new service process. This process houses the hardware drivers
    and compositor. In order to do this, a whole new subsection of Monado called
    ipc
    was added. It lives in `src/xrt/ipc` and sits between the state trackers
    and
    the service hosting the drivers and compositor.
    ([!295](https://gitlab.freedesktop.org/monado/monado/merge_requests/295))
  - Support optional systemd socket-activation: if not disabled at configure time,
    `monado-service` can be launched by systemd as a service with an associated
    socket. If the service is launched this way, it will use the systemd-created
    domain socket instead of creating its own. (If launched manually, it will still
    create its own as normal.) This allows optional auto-launching of the service
    when running a client (OpenXR) application. Associated systemd unit files are
    also included.
    ([!306](https://gitlab.freedesktop.org/monado/monado/merge_requests/306))
- XRT Interface
  - Add a new settings interface for transporting camera settings, in
    `xrt/xrt_settings.h`.
    ([!266](https://gitlab.freedesktop.org/monado/monado/merge_requests/266))
  - Make it possible to send JSON object to drivers when probing for devices.
    ([!266](https://gitlab.freedesktop.org/monado/monado/merge_requests/266))
  - Added new `xrt_instance` object to be root object, a singleton that allows us
    to
    better swap out the whole stack underneath the state trackers. This is now
    implemented by the `xrt_prober` code and used by the OpenXR state tracker.
    ([!274](https://gitlab.freedesktop.org/monado/monado/merge_requests/274))
  - Remove the `struct timestate` argument from the `struct xrt_device` interface.
    It should be easy to write a driver and the state tracker should be the one
    that tracks this state. It was mostly triggered by the out of process
    compositor work.
    ([!280](https://gitlab.freedesktop.org/monado/monado/merge_requests/280))
  - Add the new format `XRT_FORMAT_UYVY422` to the interface and various parts of
    the code where it is needed to be supported, like the conversion functions and
    calibration code. Also rename the `XRT_FORMAT_YUV422` to `XRT_FORMAT_YUYV422`.
    ([!283](https://gitlab.freedesktop.org/monado/monado/merge_requests/283))
  - Expose manufacturer and serial number in the prober interface, right now only
    for the video device probing. But this is the only thing that really requires
    this in order to tell different cameras apart.
    ([!286](https://gitlab.freedesktop.org/monado/monado/merge_requests/286))
  - Add `XRT_CAST_PTR_TO_OXR_HANDLE` and `XRT_CAST_OXR_HANDLE_TO_PTR` macros to
    perform warning-free conversion between pointers and OpenXR handles, even on
    32-bit platforms. They should be used instead of raw casts.
    ([!294](https://gitlab.freedesktop.org/monado/monado/merge_requests/294))
  - Remove declaration and implementations of `xrt_prober_create`: the minimal
    functionality previously performed there should now be moved to
    `xrt_instance_create`.
    ([!347](https://gitlab.freedesktop.org/monado/monado/merge_requests/347))
- State Trackers
  - gui: Fix compilation issue in `st/gui` when building without OpenCV.
    ([#63](https://gitlab.freedesktop.org/monado/monado/issues/63),
    [!256](https://gitlab.freedesktop.org/monado/monado/merge_requests/256))
  - OpenXR: Don't return struct with invalid type from
    `xrEnumerateViewConfigurationViews`.
    ([!234](https://gitlab.freedesktop.org/monado/monado/merge_requests/234))
  - prober: Print more information from the prober when spewing.
    ([!261](https://gitlab.freedesktop.org/monado/monado/merge_requests/261))
  - gui: Save camera and calibration data using new settings structs and format.
    ([!266](https://gitlab.freedesktop.org/monado/monado/merge_requests/266))
  - prober: Load tracking config from json and use new settings struct.
    ([!266](https://gitlab.freedesktop.org/monado/monado/merge_requests/266))
  - gui: Fix name not being shown when video device does not have any modes.
    ([!269](https://gitlab.freedesktop.org/monado/monado/merge_requests/269))
  - gui: Remove old video test scene, never used and seemed to be broken.
    ([!275](https://gitlab.freedesktop.org/monado/monado/merge_requests/275))
  - gui: Fix build when OpenCV is not available or disabled.
    ([!292](https://gitlab.freedesktop.org/monado/monado/merge_requests/292))
  - OpenXR: Fix build when OpenGL is not enabled.
    ([!292](https://gitlab.freedesktop.org/monado/monado/merge_requests/292))
  - OpenXR: Validate that we support the given `XR_ENVIRONMENT_BLEND_MODE` as
    according to the OpenXR spec. And better print the error messages.
    ([!345](https://gitlab.freedesktop.org/monado/monado/merge_requests/345))
  - OpenXR: Validate given `displayTime` in `xrEndFrame` as required by the spec.
    ([!345](https://gitlab.freedesktop.org/monado/monado/merge_requests/345))
  - OpenXR: Validate internal state that we get from the compositor.
    ([!345](https://gitlab.freedesktop.org/monado/monado/merge_requests/345))
  - OpenXR: Validate time better in xrConvertTimeToTimespecTimeKHR and add better
    error print.
    ([!348](https://gitlab.freedesktop.org/monado/monado/merge_requests/348))
  - OpenXR: Correctly translate the `XrSwapchainCreateFlags` flags to xrt ones.
    ([!349](https://gitlab.freedesktop.org/monado/monado/merge_requests/349))
  - OpenXR: In order to be able to correctly validate `XrPath` ids turn them
    into a atom and keep all created paths in a array.
    ([!349](https://gitlab.freedesktop.org/monado/monado/merge_requests/349))
  - OpenXR: Give better error messages on invalid poses in quad layers instead of
    using the simple macro.
    ([!350](https://gitlab.freedesktop.org/monado/monado/merge_requests/350))
  - OpenXR: Validate poses for project layer views, using the same expressive error
    messages as the quad layers.
    ([!350](https://gitlab.freedesktop.org/monado/monado/merge_requests/350))
  - OpenXR: Translate the swapchain usage bits from OpenXR enums to Monado's
    internal enums.
    ([!350](https://gitlab.freedesktop.org/monado/monado/merge_requests/350))
  - OpenXR: Report a spec following amount of maximum layers supported.
    ([!354](https://gitlab.freedesktop.org/monado/monado/merge_requests/354))
  - OpenXR: Correctly reject invalid times given to `xrLocateSpace`.
    ([!354](https://gitlab.freedesktop.org/monado/monado/merge_requests/354))
  - OpenXR: Correctly handle the space relation flag bits, some old hacked up code
    left over since Monado's first days have been removed.
    ([!356](https://gitlab.freedesktop.org/monado/monado/merge_requests/356))
- Drivers
  - dd: Add a driver for the Google Daydream View controller.
    ([!242](https://gitlab.freedesktop.org/monado/monado/merge_requests/242))
  - all: Use new pre-filter and 3-DoF filter in drivers.
    ([!249](https://gitlab.freedesktop.org/monado/monado/merge_requests/249))
  - arduino: Added a Arduino based flexible input device driver, along with
    Arduino C++ code for it.
    ([!251](https://gitlab.freedesktop.org/monado/monado/merge_requests/251))
  - psmv: Use all 6 measurements to compute acceleration bias, and port to new
    IMU prefilter.
    ([!255](https://gitlab.freedesktop.org/monado/monado/merge_requests/255))
  - v4l2: Add special tweaks for the ELP camera.
    ([!266](https://gitlab.freedesktop.org/monado/monado/merge_requests/266))
  - vive: Add basic 3DOF driver for Vive Wand Controller with full input support
    and
    Valve Index Controller with partial input support.
    ([!281](https://gitlab.freedesktop.org/monado/monado/merge_requests/281))
  - psvr: Use a better 3dof fusion for the PSVR when no tracking is available.
    ([!282](https://gitlab.freedesktop.org/monado/monado/merge_requests/282))
  - psvm: Move the led and rumble updating from the application facing
    update_inputs
    function to the internal thread instead.
    ([!287](https://gitlab.freedesktop.org/monado/monado/merge_requests/287))
  - psmv: Fix failure to build from source on PPC.
    ([!288](https://gitlab.freedesktop.org/monado/monado/merge_requests/288),
    [#69](https://gitlab.freedesktop.org/monado/monado/issues/69))
- Compositor
  - main: Fix XCB memory leaks and correctly use XCB/Xlib interop.
    ([!257](https://gitlab.freedesktop.org/monado/monado/merge_requests/257))
  - main: Shorten Vulkan initializers.
    ([!259](https://gitlab.freedesktop.org/monado/monado/merge_requests/259))
  - main: Port XCB and direct mode back ends to plain C.
    ([!262](https://gitlab.freedesktop.org/monado/monado/merge_requests/262))
  - main: Add support for Vive Pro, Valve Index, Oculus DK1, DK2 and CV1 to NVIDIA
    direct mode.
    ([!263](https://gitlab.freedesktop.org/monado/monado/merge_requests/263))
  - client: Make sure that the number of images is decided by the fd compositor.
    ([!270](https://gitlab.freedesktop.org/monado/monado/merge_requests/270))
  - main: Split RandR and NVIDIA direct mode window back ends.
    ([!271](https://gitlab.freedesktop.org/monado/monado/merge_requests/271))
  - main: Improve synchronization and remove redundant vkDeviceWaitIdle calls.
    ([!277](https://gitlab.freedesktop.org/monado/monado/merge_requests/277))
  - main: Delay the destruction of swapchains until a time where it is safe, this
    allows swapchains to be destroyed from other threads.
    ([!278](https://gitlab.freedesktop.org/monado/monado/merge_requests/278))
  - client: Propegate the supported formats from the real compositor to the client
    ones. ([!282](https://gitlab.freedesktop.org/monado/monado/merge_requests/282))
  - renderer: Change the idle images colour from bright white to grey.
    ([!282](https://gitlab.freedesktop.org/monado/monado/merge_requests/282))
  - main: Add support for multiple projection layers.
    ([!340](https://gitlab.freedesktop.org/monado/monado/merge_requests/340))
  - main: Implement quad layers.
    ([!340](https://gitlab.freedesktop.org/monado/monado/merge_requests/340))
  - main: Only allocate one image for static swapchains.
    ([!349](https://gitlab.freedesktop.org/monado/monado/merge_requests/349))
- Helper Libraries
  - tracking: Add image undistort/normalize cache mechanism, to avoid needing to
    remap every frame.
    ([!255](https://gitlab.freedesktop.org/monado/monado/merge_requests/255))
  - tracking: Improve readability and documentation of IMU fusion class.
    ([!255](https://gitlab.freedesktop.org/monado/monado/merge_requests/255))
  - u/file: Add file helpers to load files from config directory.
    ([!266](https://gitlab.freedesktop.org/monado/monado/merge_requests/266))
  - u/json: Add bool getter function.
    ([!266](https://gitlab.freedesktop.org/monado/monado/merge_requests/266))
  - tracking: Expose save function with non-hardcoded path for calibration data.
    ([!266](https://gitlab.freedesktop.org/monado/monado/merge_requests/266))
  - tracking: Remove all path hardcoded calibration data loading and saving
    functions.
    ([!266](https://gitlab.freedesktop.org/monado/monado/merge_requests/266))
  - threading: New helper functions and structs for doing threaded work, these are
    on a higher level then the one in os wrappers.
    ([!278](https://gitlab.freedesktop.org/monado/monado/merge_requests/278))
  - threading: Fix missing `#``pragma once` in `os/os_threading.h`.
    ([!282](https://gitlab.freedesktop.org/monado/monado/merge_requests/282))
  - u/time: Temporarily disable the time skew in time state and used fixed offset
    instead to fix various time issues in `st/oxr`. Will be fixed properly later.
    ([!348](https://gitlab.freedesktop.org/monado/monado/merge_requests/348))
  - math: Correctly validate quaternion using non-squared "length" instead of
    squared "length", certain combinations of elements would produce valid regular
    "length" but not valid squared ones.
    ([!350](https://gitlab.freedesktop.org/monado/monado/merge_requests/350))
- Misc. Features
  - build: Refactor CMake build system to make static (not object) libraries and
    explicitly describe dependencies.
    ([!233](https://gitlab.freedesktop.org/monado/monado/merge_requests/233),
    [!237](https://gitlab.freedesktop.org/monado/monado/merge_requests/237),
    [!238](https://gitlab.freedesktop.org/monado/monado/merge_requests/238),
    [!240](https://gitlab.freedesktop.org/monado/monado/merge_requests/240))
  - os/ble: Add utility functionality for accessing Bluetooth Low-Energy (Bluetooth
    LE or BLE) over D-Bus, in `os/os_ble.h` and `os/os_ble_dbus.c`.
    ([!242](https://gitlab.freedesktop.org/monado/monado/merge_requests/242))
  - util: Add some bit manipulation helper functions in `util/u_bitwise.c` and
    `util/u_bitwise.c`.
    ([!242](https://gitlab.freedesktop.org/monado/monado/merge_requests/242))
  - tracking: Make stereo_camera_calibration reference counted, and have the
    prober,
    not the calibration, call the save function.
    ([!245](https://gitlab.freedesktop.org/monado/monado/merge_requests/245))
  - math: Expand algebraic math functions in `math/m_api.h`, `math/m_vec3.h` and
    `math/m_base.cpp`.
    ([!249](https://gitlab.freedesktop.org/monado/monado/merge_requests/249))
  - math: Add pre-filter and a simple understandable 3-DoF fusion filter.
    ([!249](https://gitlab.freedesktop.org/monado/monado/merge_requests/249))
  - build: Enable the build system to install `monado-cli` and `monado-gui`.
    ([!252](https://gitlab.freedesktop.org/monado/monado/merge_requests/252))
  - build: Unify inputs for generated files between CMake and Meson builds.
    ([!252](https://gitlab.freedesktop.org/monado/monado/merge_requests/252))
  - build: Support building with system cJSON instead of bundled copy.
    ([!284](https://gitlab.freedesktop.org/monado/monado/merge_requests/284),
    [#62](https://gitlab.freedesktop.org/monado/monado/issues/62))
  - ci: Perform test builds using the Android NDK (for armeabi-v7a and armv8-a).
    This is not a full Android port (missing a compositor, etc) but it ensures we
    don't add more Android porting problems.
    ([!292](https://gitlab.freedesktop.org/monado/monado/merge_requests/292))
- Misc. Fixes
  - os/ble: Check if `org.bluez` name is available before calling in
    `os/os_ble_dbus.c`.
    ([#65](https://gitlab.freedesktop.org/monado/monado/issues/65),
    [#64](https://gitlab.freedesktop.org/monado/monado/issues/64),
    [!265](https://gitlab.freedesktop.org/monado/monado/merge_requests/265))
  - README: Added information to the README containing OpenHMD version requirement
    and information regarding the requirement of `GL_EXT_memory_object_fd` and
    limitations on Monado's compositor.
    ([!4](https://gitlab.freedesktop.org/monado/monado/merge_requests/4))
  - build: Fix build issues and build warnings when 32-bit.
    ([!230](https://gitlab.freedesktop.org/monado/monado/merge_requests/230))
  - os/ble: Fix crash due to bad dbus path, triggered by bad return checking when
    probing for BLE devices.
    ([!247](https://gitlab.freedesktop.org/monado/monado/merge_requests/247))
  - d/dd: Use the correct time delta in DayDream driver.
    ([!249](https://gitlab.freedesktop.org/monado/monado/merge_requests/249))
  - doc: Stop changelog snippets from showing up in 'Related Pages'
    ([!253](https://gitlab.freedesktop.org/monado/monado/merge_requests/253))
  - build: Fix meson warnings, increase compiler warning level.
    ([!258](https://gitlab.freedesktop.org/monado/monado/merge_requests/258))
  - os/ble: Fix leak in `os/os_ble_dbus.c` code when failing to find any device.
    ([!264](https://gitlab.freedesktop.org/monado/monado/merge_requests/264))
  - os/ble: Make ble code check for some error returns in `os/os_ble_dbus.c`.
    ([!265](https://gitlab.freedesktop.org/monado/monado/merge_requests/265))
  - u/hashset: Fix warnings in `util/u_hashset.h` after pedantic warnings were
    enabled for C++.
    ([!268](https://gitlab.freedesktop.org/monado/monado/merge_requests/268))
  - build: Fix failure to build from source on ppc64 and s390x.
    ([!284](https://gitlab.freedesktop.org/monado/monado/merge_requests/284))
  - build: Mark OpenXR runtime target in CMake as a MODULE library, instead of a
    SHARED library.
    ([!284](https://gitlab.freedesktop.org/monado/monado/merge_requests/284))
  - windows: Way way back when Gallium was made `auxiliary` was named `aux` but
    then
    it was ported to Windows and it was renamed to `auxiliary` since Windows
    is [allergic to filenames that match its device names](https://docs.microsoft.com/en-us/windows/win32/fileio/naming-a-file#naming-conventions)
    (e.g., `AUX`, `CON`, `PRN`, etc.). Through the ages, this knowledge was lost
    and so we find ourselves with the same problem. Although Monado inherited
    the correct name, the same old mistake was made in docs.
    ([!314](https://gitlab.freedesktop.org/monado/monado/merge_requests/314))
  - build: For CMake rename (nearly) all build options so they begin with `XRT_`
    and match the defines used in the source. You will probably want to clear
    your build directory and reconfigure from scratch.
    ([!327](https://gitlab.freedesktop.org/monado/monado/merge_requests/327))
  - ipc: Correctly set the shared semaphore value when creating it, the wrong value
    resulted in the client not blocking in `xrWaitFrame`.
    ([!348](https://gitlab.freedesktop.org/monado/monado/merge_requests/348))
  - ipc: Previously some arguments where dropped at swapchain creation time,
    correct pass them between the client and server.
    ([!349](https://gitlab.freedesktop.org/monado/monado/merge_requests/349))

## Monado 0.1.0 (2020-02-24)

Initial (non-release) tag to promote/support packaging.

