OpenXR: Add disabled `XR_MSFT_hand_intertaction`.
The binding code has support for this extension, but the bindings are not
used in any of the drivers so totally untested and would lead to the wrong
expectations of the applications.
