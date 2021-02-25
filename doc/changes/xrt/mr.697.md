Added frame timing code that when the underlying vulkan driver supports the
VK_GOOGLE_display_timing extension greatly improves the timing accerecy of the
compositor. Along with this tracing code was added to better help use understand
what was happening during a frame.
