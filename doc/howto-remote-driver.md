# How to use the remote driver {#howto-remote-driver}

<!--
Copyright 2022, Collabora, Ltd. and the Monado contributors
SPDX-License-Identifier: BSL-1.0
-->

## Prerequisites

Before proceeding you will need to have monado installed (or built) and capable
of running applications. If you do not have any hardware this should still work
with the simulated driver. For those building Monado themselves you have to make
sure the GUI is built. In short the commands `monado-gui` and `monado-service`
are needed.

## Running

Open up three terminals and in the first run this command:

```bash
P_OVERRIDE_ACTIVE_CONFIG="remote" <path-to>/monado-serivce
```

If you get a error saying `ERROR [u_config_json_get_remote_port] No remote node`
you can safely ignore that. Once it is up and running you can now start and
connect the controller GUI. Select the second terminal use the command below and
click connect.

```bash
monado-gui remote
```

You can now launch the program. You technically don't need to launch the
program after @p monado-gui, once the service is running they can be launched
in any order.

```bash
hello_xr -G Vulkan2
```

Now you can manipulate the values and control the devices.
