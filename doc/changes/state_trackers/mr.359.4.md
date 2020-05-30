OpenXR: Make the event queue thread safe, all done with a simple mutex that is
not held for long at all.
