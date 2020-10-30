# Roadmap

<!--
Copyright 2018-2020, Collabora, Ltd. and the Monado contributors
SPDX-License-Identifier: BSL-1.0
-->

## Short term

* **cmake**: Make a proper FindXCB.cmake file.
* **@ref comp**: Do timing based of the display refresh-rate and display time.
* **@ref comp**: Move into own thread.
* **@ref oxr**: Locking, maybe we just have a single lock for the session.
                We will need to figure out how to do wait properly.
* **@ref oxr**: Complete action functions.

## Long term

* **aux/beacon**: Complete and integrate Lighthouse tracking code.
* **@ref comp**: Support other extensions layers.
* **@ref comp**: See-through support for Vive headset.
* **doc**: Group Related code.
* **doc**: Lots of documentation for runtime.
* **@ref drv**: Port rest of OpenHMD drivers to our runtime.
* **progs**: Settings and management daemon.
* **progs**: Systray status indicator for user to interact with daemon.
* **progs**: Room-scale setup program.
