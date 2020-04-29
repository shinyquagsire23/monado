Add support for a new service process. This process houses the hardware drivers
and compositor. In order to do this, a whole new subsection of Monado called ipc
was added. It lives in `src/xrt/ipc` and sits between the state trackers and
the service hosting the drivers and compositor.
