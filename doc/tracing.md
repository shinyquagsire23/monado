# Tracing support {#tracing}

<!--
Copyright 2021, Collabora, Ltd. and the Monado contributors
SPDX-License-Identifier: BSL-1.0
-->

## Requirements

Monado uses the [Perfetto][]/[Percetto][] framework for tracining support, you
need to first build and install [Percetto][] in a place where CMake can find it.
Build [Perfetto][] (you will have gotten the source at least  as part of build
[Percetto][]). It is a good idea to familiarise yourself with Perfetto before
proceeding. You then need to build Monado with CMake and give make sure
`XRT_FEATURE_TRACING` is enabled.

* Build and install [Percetto][].
* Build and get [Perfetto][] running.
* Build Monado with CMake and with `XRT_FEATURE_TRACING` being `ON`.

## Running

Save the following file to `data_events.cfg`, next to your perfetto folder.
Please refer to [Perfetto][] documentation about the format and options of this
config file, but the most important bits is the `tracker_event` section.

```c
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
# Not needed with the above config.
./perfetto/out/linux_clang_release/traced_probes &

# When you want to run a capture do and then run Monado.
./perfetto/out/linux_clang_release/perfetto --txt -c data_events.cfg -o /tmp/trace.protobuf
```

[Perfetto]: https://perfetto.dev
[Percetto]: https://github.com/olvaffe/percetto
