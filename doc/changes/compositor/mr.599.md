main: Refactor how the compositor interacts with targets, the goal is to enable
the compositor to render to destinations that isn't backed by a `VkSwapchain`.
Introduce `comp_target` and remove `comp_window`, also refactor `vk_swapchain`
to be a sub-class of `comp_target` named `comp_target_swapchain`, the window
backends now sub class `comp_target_swapchain`.
