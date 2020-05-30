OpenXR: A major overhaul of the swapchain acquire, wait and release code. This
makes it almost completely conformant with the spec. Tricky parts include that
multiple images can be acquired, but only one can be waited on before being
released.
