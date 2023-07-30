Remove the `xrt_gfx_native.h` as it is no longer needed, it has been replaced
by `compositor/main` own interface file. In the past it was the state tracker
or IPC layer that created the `xrt_system_compositor` directly by calling this
function. But now it's the `xrt_instance`s responsibility, and it can pick
which compositor to create. So this interface becomes less special and just
one of many possible compositors implementations.
