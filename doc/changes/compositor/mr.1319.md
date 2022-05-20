null: Add a new compositor intended to be used on CIs that use the Mesa software
rasteriser vulkan driver. It is also intended to be a base for how to write a
new compositor. It does no rendering and does not open up any window, so has
less requirements then the main compositor, both in terms of CPU usage and build
dependencies.
