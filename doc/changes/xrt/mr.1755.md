Introduce `xrt_layer_frame_data` struct that holds per frame data for the
`xrt_compositor` layer interface. This is a sibling to the `xrt_layer_data`
struct which holds per layer data. Both are structs instead of arguments to make
it easier to pass the needed data through the layers of Monado, and for easier
extension further down the line.
