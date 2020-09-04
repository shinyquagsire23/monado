Remove the `linear_acceleration` and `angular_acceleration` fields from the
`xrt_space_relation` struct, these were not used in the codebase and are not
exposed in the OpenXR API. They can easily be added back should they be
required again by code or a future feature. Drivers are free to retain this
information internally, but no longer expose it.
