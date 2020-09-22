Replace mesh generator with `compute_distortion` function on `xrt_device`. This
is used to both make it possible to use mesh shaders for devices and to provide
compatibility with SteamVR which requires a `compute_distortion` function as
well.

The compositor uses this function automatically to create a mesh and uses mesh
distortion for all drivers. The function `compute_distortion` default
implementations for `none`, `panotools` and `vive` distortion models are
provided in util.
