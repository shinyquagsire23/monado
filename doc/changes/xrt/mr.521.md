Remove the `out_timestamp` argument to the `xrt_device::get_tracked_pose`
function, it's not needed anymore and the devices can do prediction better
as it knows more about it's tracking system the the state tracker.
