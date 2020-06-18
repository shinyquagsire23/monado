main: We were alpha blending all layers previously, but now we support the
layer flag that OpenXR gives us. We do this by using different `VkImageView`s
with different component swizzles.
