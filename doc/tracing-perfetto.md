# Tracing with Perfetto {#tracing-perfetto}

<!--
Copyright 2021-2023, Collabora, Ltd. and the Monado contributors
SPDX-License-Identifier: BSL-1.0
-->

## Requirements

Monado uses the [Perfetto][]/[Percetto][] framework for tracing support. You
need to first build and install [Percetto][] in a place where CMake can find it.
Build [Perfetto][] (you will have gotten the source at least as part of build
[Percetto][]). It is a good idea to familiarise yourself with Perfetto before
proceeding. You then need to build Monado with CMake and make sure
`XRT_FEATURE_TRACING` is enabled.

* Build and install [Percetto][] - **note** Depending on the version of Percetto
  you are using you might need to have a release version of Perfetto available
  or use the one that is included in percetto. A release version of Perfetto is
  needed due to the `sdk/` directory and amalgamated files is only made there.
* Build and get [Perfetto][] running.
* Build Monado with CMake and with `XRT_FEATURE_TRACING` being `ON`.

## Running

Save the following file to `data_events.cfg`, next to your perfetto folder.
Please refer to [Perfetto][] documentation about the format and options of this
config file, but the most important bits is the `tracker_event` section.

```none
flush_period_ms: 30000

incremental_state_config {
	clear_period_ms: 500
}

buffers: {
	size_kb: 63488
	fill_policy: DISCARD
}

# This is the important bit, this enables all data events from Monado.
data_sources: {
	config: {
		name: "track_event"
		target_buffer: 0
	}
}
```

Then run the following commands before launching Monado.

```bash
# Start the daemon.
# Only needs to be run once and keeps running.
./perfetto/out/linux_clang_release/traced &

# Start the daemon ftrace probes daemon.
# Only needs to be run once and keeps running.
# Not needed with the preceding config.
./perfetto/out/linux_clang_release/traced_probes &

# When you want to run a capture do and then run Monado.
./perfetto/out/linux_clang_release/perfetto --txt -c data_events.cfg -o /tmp/trace.protobuf
```

Finally run the App and Monado with `XRT_TRACING=true` exported.

```bash
XRT_TRACING=true monado-serivce
```

## Gotchas

Here's where we write down bugs or other sharp corners that we found while
running Monado with [Perfetto][]/[Percetto][] and tracing enabled.

### OpenXR CTS

Running multiple CTS tests in one run causes Perfetto to crash, this is because
the CTS loads and unloads the OpenXR runtime multiple times, and there seems to
be a race on destruction.

### "Value doesn't exist" in web viewer

This is probably because you don't have read permissions on your tracefile,
probably because you ran traced/tracebox as root. Don't do that, instead do
`sudo chown -R $USER /sys/kernel/tracing` and run traced/tracebox as your normal
user.

(If you really have to run it as root, then before you open the tracefile do
`sudo chown $USER <tracefile>`).


[Perfetto]: https://perfetto.dev
[Percetto]: https://github.com/olvaffe/percetto
