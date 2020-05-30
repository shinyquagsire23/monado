util: Make sure to not access NULL control messages, say in the case of the
server failing to create a swapchain. Also add a whole bunch of paranoia when
it comes to the alignment of the control message buffers.
