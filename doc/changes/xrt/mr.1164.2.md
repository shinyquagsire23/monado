Add `xrt_compositor_semaphore` object, add interfaces to `xrt_compositor` for
creating the new semaphore object. Also add interface for passing in semaphore
to `xrt_compositor::layer_commit`. Added support for these interface through
the whole Monado stack.
