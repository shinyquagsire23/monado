OpenXR: When a `XrSession` is destroyed purge the event queue of any events that
references to it so that no events gets delivered to the applications with
stales handles.
