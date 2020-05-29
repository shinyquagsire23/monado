Add `XRT_CAST_PTR_TO_OXR_HANDLE` and `XRT_CAST_OXR_HANDLE_TO_PTR` macros to
perform warning-free conversion between pointers and OpenXR handles, even on
32-bit platforms. They should be used instead of raw casts.
