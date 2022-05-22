No longer include any util headers (in this case `u_time.h`), the XRT headers
are supposed to be completely self contained. The include also messed with build
refactoring in the auxiliary directory.
