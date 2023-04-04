client: Don't use vkDeviceWaitIdle, because it requires all queues to be
externally synchronized which we can't enforce.
