Extend `xrt_swapchain_create_properties` to allow the main compositor request
extra bits to be used beyond those requested by the OpenXR app. Some compositors
might need extra usage bits set beyond just the constant sampled bit. This also
ensures that images have exactly the same usages in both the compositor and app.
