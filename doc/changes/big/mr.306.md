Support optional systemd socket-activation: if not disabled at configure time,
`monado-service` can be launched by systemd as a service with an associated
socket. If the service is launched this way, it will use the systemd-created
domain socket instead of creating its own. (If launched manually, it will still
create its own as normal.) This allows optional auto-launching of the service
when running a client (OpenXR) application. Associated systemd unit files are
also included.
