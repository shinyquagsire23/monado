Add `xrt_multi_compositor_control` that allows the `xrt_system_compositor` to
expose a interface that the IPC layer can use to manage multiple clients
without having to do the rendering. This allows us to move a lot of the code
out the IPC layer and make it more about passing calls.
