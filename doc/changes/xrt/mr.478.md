Add `xrt_image_native_allocator` as a friend to the compositor interface. This
simple interface is intended to be used by the IPC interface to allocate
`xrt_image_native` on the client side and send those to the service.
