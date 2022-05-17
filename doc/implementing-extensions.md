# Implementing OpenXR extensions {#implementing-extension}

<!--
Copyright 2021, Collabora, Ltd. and the Monado contributors
SPDX-License-Identifier: BSL-1.0
-->

Khronos often adds new functionality to the OpenXR specification as extensions.

The general steps to implement an OpenXR extension in Monado are as follows.

* Edit scripts/generate_oxr_ext_support.py. Usually you only need to add an
  entry to the `EXTENSIONS` list at the top.
* Run the script `python scripts/generate_oxr_ext_support.py`.
* Format the regenerated file with
  `clang-format -i src/xrt/state_trackers/oxr/oxr_extension_support.h`.
* Add entry points for each new function in
  `src/xrt/state_trackers/oxr/oxr_api_negotiate.c`.
* Add Monado internal prototypes for the new functions in
  `src/xrt/state_trackers/oxr/oxr_api_funcs.h`. The Monado implementations of
  OpenXR functions are prefixed with `oxr_` and use the `XRAPI_ATTR` and
  `XRAPI_CALL` macros to satisfy calling conventions for all platforms.
* Implement the `oxr_` Monado internal functions in an appropriate source file
  `src/state_trackers/oxr/oxr_api_*.c`. Trivial functions can be implemented
  right there along with the usual parameter checks. More complex functions that
  use more internal Monado state should call functions implemented in the
  relevant `oxr_*.c` file (without `_api_`). The definitions for those functions
  go into `src/xrt/state_trackers/oxr/oxr_objects.h`.
* Monado internal implementations of "objects" (think XrSession or
  XrHandTracker) go into `src/xrt/state_trackers/oxr/oxr_objects.h`.
* Enums, defines and types go into `src/xrt/include/xrt/xrt_defines.h`. OpenXR
  types are typically not used outside of the `oxr_api_*` files, instead
  equivalents with the prefix `XRT_` are defined here.
