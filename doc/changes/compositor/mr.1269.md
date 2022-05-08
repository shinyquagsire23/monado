main: Refactor comp_target_swapchain to not pre-declare internal functions, we
seem to be moving away from this style in the compositor so refactor the
`comp_target_swapchain` file before adding the vblank thread in there.
