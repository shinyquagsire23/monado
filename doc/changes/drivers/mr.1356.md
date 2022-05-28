remote: Greatly improve the remote driver. Properly shutting down the main loop.
Use the new @ref xrt_system_devices as base class for @ref r_hub. Exposing the
Valve Index Controller instead of the simple controller as it better allows to
map other controllers to it. Reusing the vive bindings helper library.
