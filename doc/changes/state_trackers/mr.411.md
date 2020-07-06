OpenXR: Fix overly attached action sets, which would appear to be attached to
a session even after the session has been destroyed. Also tidy up comments and
other logic surrounding this.
