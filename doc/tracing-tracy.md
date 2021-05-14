# Tracing with Tracy {#tracing-tracy}

<!--
Copyright 2022-2023, Collabora, Ltd. and the Monado contributors
SPDX-License-Identifier: BSL-1.0
-->

## Requirements

One of the tracing backends Monado has is [Tracy][]. Note, that the backend is
fully integrated so you only need to make sure that the correct CMake flags are
set, `XRT_HAVE_TRACY` and `XRT_FEATURE_TRACING`. Building the [Tracy][] profiler
is also required, but the order doesn't matter.

* Get the [Tracy][] profiler.
  * **Linux**: build it, needs `glfw3`, `freetype2` & `capstone` dev packages.
  * **Windows**: download release binaries from page, or build it.
* Build Monado with `XRT_FEATURE_TRACING` & `XRT_HAVE_TRACY` being `ON`.

## Running

Start the [Tracy][] profiler, either try to connect directly to the host you
want to run Monado on, or wait for it to show up in the first initial UI. Then
run Monado like you normally would, for example as follows.

```bash
monado-serivce
```

## Notes

Unlike @ref tracing-perfetto Tracy supports Windows, it also supports live
viewing of the data stream. But Tracy can only trace one application at a time,
whereas Perfetto can do multiple processes at the same time and whole system
tracing, giving a higher level overview of the whole system.

[Tracy]: https://github.com/wolfpld/tracy
