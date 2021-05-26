client: Use the EGL compositor's display in swapchain, previously it tried to
use the current one, which when running on a new thread would explode.
