# Frame Pacing/Timing {#frame-pacing}

<!--
Copyright 2021, Collabora, Ltd. and the Monado contributors
SPDX-License-Identifier: BSL-1.0
-->

A "brief" overview of the various time-points that a frame goes through, from
when the application gets go ahead to render the frame to when pixels are turned
into photons. This is only a single frame, where all of the timings are achieved and
the application is single threaded. The HMD also only turns on the display
during the vblank period, meaning the pixel to photon transformation is delayed
from scanout starting to the vblank period (like for the Index).

* `xrWaitFrame` returns to the application, referred to as **wake_up**.
* The app does a logic step to move the simulation to the next predicted
  display time.
* `xrBeginFrame` is called by the application, referred to as **begin**.
* The app renders the current views using the GPU.
* `xrEndFrame` is called by the application submitting the views.
* The compositor wakes up to do its distorting rendering and any warping,
  checking if the applications rendering has finished. When the compositor
  submits the work to the GPU, referred to as **submit**.
* The compositor queues its swapbuffer to the display engine.
* Scanout starts, the kernel checked if the compositors rendering was completed
  in time. We refer to this time as **present**, this seems to be common.
* During the vblank period the display lights up turning the pixels into
  photons. We refer to this time as **display**, same as in OpenXR.

The names for timepoints are chosen to align with the naming in
[`VK_GOOGLE_display_timing`][], reading that extension can provide further
information.

## Main compositor perspective

* @ref xrt_comp_wait_frame - It is within this function that the frame timing is
  predicted, see @ref u_pa_predict and @ref u_pc_predict. The compositor will
  then wait to **wake_up** time and then return from this function.
* @ref xrt_comp_begin_frame - The app or IPC server calls this function when it
  is done with CPU work and ready to do GPU work.
* @ref xrt_comp_discard_frame - The frame is discarded.
* @ref xrt_comp_layer_begin - Called during transfers of layers.
* @ref xrt_comp_layer_stereo_projection - This and other layer functions are
  called to list the layers the compositor should render.
* @ref xrt_comp_layer_commit - The compositor starts to render the frame,
  trying to finish at the **present** time.

[`VK_GOOGLE_display_timing`]: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_GOOGLE_display_timing.html
