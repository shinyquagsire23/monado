# Writing a new driver {#writing-driver}

<!--
Copyright 2018-2021, Collabora, Ltd. and the Monado contributors
SPDX-License-Identifier: BSL-1.0
-->

## Map

The components you will be interacting with is @ref st_prober to find the
hardware devices and setup a working system, along with the @ref aux code that
provides various helpers. You will actually be implementing the @ref xrt_device
interface by writing a driver. It is convention in Monado for interfaces to
allow full, complete control of anything a device might want to modify/control,
and to provide helper functionality in @ref aux to simplify implementation of
the most common cases.

## Getting started

The easiest way to begin writing a driver is to start from a working example.
The @ref drv_sample driver is provided explicitly for this purpose: it creates
an HMD device, with a custom @ref xrt_auto_prober implementation for hardware
discovery, and some simple display parameters that should be easy to modify.

Copy that directory and rename the files in it. Then, use the following `sed`
command to perform some bulk renames before you begin actually writing code. The
command as written assumes your new device type is called `my_device` or `md`
for short, and your auto-prober is called `my_device_auto_prober` or `mdap` for
short: change the replacement side of each pattern to match the real names you
are using.

```sh
# First pattern is for renaming device types,
# second is for renaming device variables,
# third is for renaming device macros.
# Fourth and fifth are for renaming auto prober types and variables, respectively.
# The last two are for renaming the environment variable and function name
# for the environment variable logging config.
sed -r -e 's/sample_hmd/my_device/g' \
  -e 's/\bsh\b/md/g' \
  -e 's/sample_auto_prober/my_device_auto_prober/g' \
  -e 's/\bsap\b/mdap/g' \
  -e 's/\bSH_/MD_/g' \
  -e 's/sample/my_device/g' \
  -e 's/SAMPLE/MY_DEVICE/g' \
  -i *.c *.h
```

You will want to go through each function of the sample code you started from,
implement any missing functionality, and adapt any existing functionality to
match your device. Refer to other @ref drv for additional guidance. Most drivers
are fairly simple, as large or complex functionality in drivers is often
factored out into separate auxiliary libraries.

## What to Implement

You will definitely make at least one implementation of @ref xrt_device. If your
driver can talk to e.g. both a headset and corresponding controllers, you can
choose to expose all those through a single xrt_device implementation, or
through multiple implementations that may share some underlying component (by
convention called `..._system`). Both are valid choices, and the right one to
choose depends on which maps better to your underlying device or API you are
connecting to. It is more common to have one xrt_device per piece of hardware,
however. @ref hydra_device serves as a nice example of two controllers that are
enumerated as a single overall USB HID device but expose two separate xrt_device
instances.

Depending on whether your device can be created from a detected USB HID device,
you will also need to implement either @ref xrt_auto_prober or a function
matching @ref xrt_prober_found_func_t which is the function pointer type of
@ref xrt_prober_entry::found. See below for more details.

## Probing

When should I implement the @ref xrt_auto_prober interface? The answer is not
too hard: you use the auto prober interface when the basic USB VID/PID-based
interface is not sufficient for you to detect presence/absence of your device,
or if you don't want to use the built-in HID support for some reason.

If you can detect based on VID/PID, you will instead implement If you can use
built-in HID, you might consider looking at @ref hdk_found, which is a nice
example of how to implement @ref xrt_prober_found_func_t to perform
detection of an HMD based on the USB HID for its IMU.

Either way, your device's detection details will need to be added to a list used
by the prober at @ref xrt_instance startup time. The stock lists for mainline
Monado are in `src/xrt/targets/common/target_lists.c`. These are shared by the
various targets (OpenXR runtime shared library, service executable, utility
executables) also found in `src/xrt/targets`. If you're using Monado as a
toolkit or component rather than as a standalone runtime and service, you can
replicate whatever portions of the target lists in your own target, or directly
implement the @ref xrt_instance interface more directly, linking in only those
drivers and components you need. **Note**, however, that Monado is intended to
not expose any external API other than the OpenXR API: the @ref xrt_iface are
subject to change as required so those writing drivers or other software on
those interfaces are encouraged to upstream as much as possible to minimize
maintenance burden.
