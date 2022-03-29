# Spaces in Monado design {#design-spaces}

<!--
Copyright 2022-2023, Collabora, Ltd. and the Monado contributors
SPDX-License-Identifier: BSL-1.0
-->

## Goals

* Hold a super set of the semantic spaces that OpenXR requires, local, stage,
  unbounded, etc...
* Allow devices to be attached to other devices.
* Generating events when a space is moved, dealing with the requirement that
  the move only happens after a certain time in the future.
* Make the internal graph be simple and non-cyclic.
  * No bi-directional links.
  * Always have a root object.

## Overall design

The `xrt_space` object is fairly straight forward, it's a space. Some have
semantic meaning like local, stage, unbounded. The `xrt_space_overseer` object
is where most of the interesting things happens. By having a separate object
where functionality is implemented we can handle things like: different sessions
with different spaces, have a object that space events comes from. It also makes
it easier for the IPC layer to implement it.

## Issues & questions

* Where do space events come from?
  * **UNRESOLVED:** Probably will come from the space overseer.
* Do we map all spaces one to one, like the each `XrSpace` with an offset gets
  backed with a `xrt_space`. There is going to be a limit on spaces from the
  point of view of the IPC layer, do we create some of them locally?
  * **RESOLVED:** While you can create a offset `xrt_space`, the offsets from
    from the `XrSpace`s are not expressed as an `xrt_space` this is to reduce
    the number of spaces created.
* For the IPC gap do we mirror the entire space graph between the app side and
  the service side. Or is it enough to only expose the spaces/nodes a app needs
  and keep all of the links on the service side only.
  * **RESOLVED:** The graph is kept opaque from the application (and all other
    parts of Monado, an space overseer has greater freedom in how to configure
    the space graph).
