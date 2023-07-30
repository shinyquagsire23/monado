all: Use libbsd pidfile to detect running Monado instances.
Enables automatically deleting stale socket files.
The socket file is now placed in $XDG_RUNTIME_DIR/monado_comp_ipc by default
or falls back to /tmp/monado_comp_ipc again if $XDG_RUNTIME_DIR is not set.
