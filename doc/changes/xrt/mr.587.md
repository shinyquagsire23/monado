Add `xrt_binding_profile` struct, related pair structs and fields on
`xrt_device` to allow to move the static rebinding of inputs and outputs into
device drivers. This makes it easier to get a overview in the driver itself
which bindings it can bind to.
