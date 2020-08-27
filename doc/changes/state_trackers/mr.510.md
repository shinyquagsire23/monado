OpenXR: For pose actions the any path (`XR_NULL_PATH`) needs to be special
cased, essentially turning into a separate action sub path, that is assigned
at binding time.
