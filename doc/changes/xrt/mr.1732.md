Remote `xrt_prober_device::product_name` array, the value was only used
internally by the prober. There is already a access function for the product
name that is needed by USB, so make the interface less confusing.
