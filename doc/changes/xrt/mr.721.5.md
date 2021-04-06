Pass `XrFrameEndInfo::displayTime` to `xrt_compositor::layer_begin` so that the
compositor can correctly schedule frames, most importantly do not display them
too early that might lead to stutter.
