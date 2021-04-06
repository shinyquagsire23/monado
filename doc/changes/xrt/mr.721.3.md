Add alternative functions to `xrt_compositor::wait_frame` called
`xrt_compositor::predict_frame` and `xrt_compositor::mark_frame` these allow one
to do frame timing without having to block on the service side.
