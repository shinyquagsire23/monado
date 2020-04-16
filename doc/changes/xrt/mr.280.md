Remove the `struct timestate` argument from the `struct xrt_device` interface.
It should be easy to write a driver and the state tracker should be the one
that tracks this state. It was mostly triggered by the out of process
compositor work.
