xrt: Introduce `xrt_system_compositor`, it is basically a analogous to
`XrSystemID` but instead of being a fully fledged xrt_system this is only the
compositor part of it. Also fold the `prepare_session` function into the create
native compositor function to simplify the interface.
