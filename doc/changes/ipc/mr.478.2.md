client: Add a "loopback" image allocator, this code allocates a swapchain from
the service then imports that back to the service as if it was imported. This
tests both the import code and the image allocator code.
