client: When we give a image fd to the either OpenGL or Vulkan it is consumed
and can not be rused. So make sure that it is set to an invalid fd value on the
`xrt_image_fd` on the owning `xrt_swapchain_fd`.
