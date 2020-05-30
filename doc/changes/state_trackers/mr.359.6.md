OpenXR: Enforce that static swapchains can only be acquired once, this is
required by the spec and make sure that a image is only rendered to once, and
allows the runtime to perform special optimizations on the image.
