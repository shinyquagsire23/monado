# Writing a new driver

This document will tell you in broad strokes what you need to do to create a
driver in Monado. Like the many ones already in there @ref drv. It is not a step
by step guide to making a driver. Also what you need to do can vary a lot
depending on the type of hardware you are adding a driver for and the level of
features you want.

## Map

The first components you will be interacting with is @ref st_prober find the
hardware devices and setup a working system, along with the @ref aux code that
provides various helpers. You can look at other @ref drv on how to start.
