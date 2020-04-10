Added new `xrt_instance` object to be root object, a singleton that allows us to
better swap out the whole stack underneath the state trackers. This is now
implemented by the `xrt_prober` code and used by the OpenXR state tracker.
