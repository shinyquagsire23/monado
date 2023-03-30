Added functions to swapchain to explicitly do the barrier insertion.

This pushes the handling of barrier calls into the OpenXR state tracker, while
the changes are minimal for the client compositors they no longer have the
responsibility to implicitly to insert a barrier when needed. Allows us to in
the future support extensions.
