ipc: Correctly set the shared semaphore value when creating it, the wrong value
resulted in the client not blocking in `xrWaitFrame`.
