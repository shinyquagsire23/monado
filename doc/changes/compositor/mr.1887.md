- util: Make sure to not destroy invalid `VkSemaphore` objects.
- util: Track native semaphore handles, following the semantics of other handles
in Monado. This fixes the leak of `syncobj_file` on Linux.
