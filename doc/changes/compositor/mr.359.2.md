swapchain: Close any FDs that are still valid, for instance the ipc server
copies the FDs to the client.
