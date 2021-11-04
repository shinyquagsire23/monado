util: Refactor swapchain and fence code to be more independent of compositor
and put into own library. Joined by a @ref comp_base helper that implements
a lot of the more boiler-plate compositor code.
