Add `xrt_space_graph` struct for calculating space relations. This struct and
accompanying makes it easier to reason about space relations than just functions
operating directly on `xrt_space_relation`. The code base is changed to use
these new functions.
