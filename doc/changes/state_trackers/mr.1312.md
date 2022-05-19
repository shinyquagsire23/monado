OpenXR: Do not expose the XR_EXT_debug_utils extension, none of the functions
where given out but we still listed the extension to the loader. So we put it
behind a feature config that is always set to off.
