# Writing a new driver

<!--
Copyright 2018-2020, Collabora, Ltd. and the Monado contributors
SPDX-License-Identifier: BSL-1.0
-->

This document will tell you in broad strokes what you need to do to create a
driver in Monado. Like the many ones already in there @ref drv. It is not a step
by step guide to making a driver. Also what you need to do can vary a lot
depending on the type of hardware you are adding a driver for and the level of
features you want.

## Map

The first components you will be interacting with is @ref st_prober find the
hardware devices and setup a working system, along with the @ref aux code that
provides various helpers. You can look at other @ref drv on how to start.

## Probing

When should I implement the @ref xrt_auto_prober interface? The answer is not
too hard: you use the auto prober interface when the basic USB VID/PID-based
interface is not sufficient for you to detect presence/absence of your device,
or if you don't want to use the built-in HID support for some reason.
