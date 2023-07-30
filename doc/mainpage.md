# Monado Developer Documentation

<!--
Copyright 2018-2022, Collabora, Ltd. and the Monado contributors
SPDX-License-Identifier: BSL-1.0
-->

[TOC]

This documentation is intended for developers wanting to dive into the code of
Monado. It also assumes that you have read [README.md][]: that file also holds
getting started information and general documentation.

[README.md]: https://gitlab.freedesktop.org/monado/monado

This documentation is maintained in part in documentation comments in the code
itself, extracted and rendered by Doxygen. These extracted documentation pages
are best browsed through the "Modules" or "Files" links above. (The directory
structure matches the top levels of modules, but some are sub-divided further
into sub-modules within a directory.)

There are also a number of pages in this site (including this one) that are
maintained as fully human-authored Markdown files outside of source code files,
but still in the repository in the `doc/` directory. Some are linked below in a
logical outline, and some documentation comments in code cross-reference these
pages. See the "Related Pages" link above for a complete list of these
non-code-based documentation pages. (Since they are not maintained in the source
code files directly, they may sometimes be slightly out-of-date, so in case of
conflict, the code-based documentation is correct. Please submit a merge request
to fix any such issues you may notice.)

## Changelog

@ref CHANGELOG

If you are viewing this on the web at
<https://monado.pages.freedesktop.org/monado/>, the changelog above also
includes a section for changes that have not yet been in a tagged release.

## Developer Guides

* @ref conventions - to help you both read and write Monado code
* @ref writing-driver
* @ref implementing-extension
* @ref how-to-release
* @ref winbuild

## Design Documentation

Monado is architected as a collection of loosely-coupled, internally cohesive
components that interact through the internal, abstract "XRT" (XrRunTime) API.
State Trackers consume implementations of these APIs provided by other modules.

* @ref understanding-targets - How the components of Monado (`xrt_instance`,
  IPC, OpenXR, etc) are brought together for use.
* @ref ipc-design
* @ref frame-pacing

The key interfaces to begin learning Monado are:

* @ref xrt_instance
* @ref xrt_device
* @ref xrt_compositor

Here is the documentation for all @ref xrt_iface used to interact between
modules.

## Advanced Usage Information

* @ref vulkan-extensions
* @ref howto-remote-driver
* @ref tracing
* @ref metrics

## Source layout

* src/xrt/include - @ref xrt_iface defines the internal interfaces of Monado.
* src/xrt/drivers - Hardware @ref drv.
* src/xrt/compositor - @ref comp code for doing distortion and driving the
  display hardware of a device.
* src/xrt/state_trackers/oxr - @ref oxr, implements the OpenXR API.
* src/xrt/state_trackers/gui - @ref gui, various helper and debug GUI code.
* src/xrt/auxiliary - @ref aux and other larger components, like @ref
  aux_tracking and @ref aux_math.
* src/xrt/targets - glue code and build logic to produce final binaries.
* src/external - a small collection of external code and headers.
