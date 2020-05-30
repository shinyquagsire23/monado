OpenXR: Validate the subImage data for both projection and quad layers layers,
refactor code out so it can be shared with the different types of layers. Need
to track some state on the `oxr_swapchain` in order to do the checking.
