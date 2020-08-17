rs: Make the pose getting from the T265 be threaded. Before we where getting the
pose from the update input function, this would cause some the main thread to
block and would therefore cause jitter in the rendering.
