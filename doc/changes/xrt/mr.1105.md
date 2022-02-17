Add the ability for `xrt_device` to dynamically change the fov of the views
return back to the applications. We do this by addning a new function called
`xrt_device::get_view_poses` and removing the old one. This function now returns
view poses, fovs and the observer position in one go.
