# About Monado's UltraLeap driver

<!--
Copyright 2021, Moses Turner
SPDX-License-Identifier: BSL-1.0
-->

## Building

To build you need `Leap.h` and `LeapMath.h` in `/usr/local/include`; and
`libLeap.so` in `/usr/local/lib`, and this should automatically build.

## Running

To have the ultraleap driver successfully initialize, you need to have the Leap
Motion Controller plugged in, and `leapd` running. Running `sudo leapd` in
another terminal works but it may be slightly more convenient to have it run as
a systemd service.

## Configuring

Presumably, you're using this driver because you want to stick the Leap Motion
Controller on the front of your HMD and have it track your hands.

If you don't have a config file at `~/.config/monado/config_v0.json` (or
wherever you set `XDG_CONFIG_DIR`), your tracked hands will show up near the
tracking origin and not move around with your HMD, which is probably not what
you want.

Instead you probably want to configure Monado to make your Leap Motion
Controller-tracked hands follow around your HMD. There's an example of how to do
this with North Star in `doc/example_configs/config_v0.northstar_lonestar.json`.
If you're using a North Star headset, that should work but unless you're using
the Lone Star NS variant you'll need to edit the offsets. If you're using some
other HMD you'll have to edit the `tracker_device_serial` to be your HMD serial,
and your own offsets.
