# IPC Design {#ipc-design}

<!--
Copyright 2021, Collabora, Ltd. and the Monado contributors
SPDX-License-Identifier: BSL-1.0
-->

- Last updated: 24-February-2021

When the service starts, an `xrt_instance` is created and selected, a native
system compositor is initialized, a shared memory segment for device data is
initialized, and other internal state is set up. (See `ipc_server_process.c`.)

There are three main communication needs:

- The client shared library needs to be able to **locate** a running service, if
  any, to start communication. (Auto-starting, where available, is handled by
  platform-specific mechanisms: the client currently has no code to explicitly
  start up the service.)
- The client and service must share a dedicated channel for IPC calls (aka
  **RPC** - remote procedure call), typically a socket.
- The service must share access to device data updating at various rates, shared
  by all clients. This is typically done with a form of **shared memory**.

Each platform's implementation has a way of meeting each of these needs. The
specific way each need is met is highlighted below.

## Linux Platform Details

In an typical Linux environment, the Monado service can be launched one of two
ways: manually, or by socket activation (e.g. from systemd). In either case,
there is a Unix domain socket with a well-known name (known at compile time, and
built-in to both the service executable and the client shared library) used by
clients to connect to the service: this provides the **locating** function.
This socket is polled in the service mainloop, using epoll, to detect any new
client connections.

Upon a client connection to this "locating" socket, the service will [accept][]
the connection, returning an FD, which is passed to
`start_client_listener_thread()` to start a thread specific to that client. The
FD produced this way is now also used for the IPC calls - the **RPC** function -
since it is specific to that client-server communication channel. One of the
first calls made transports a duplicate of the **shared memory** segment file
descriptor to the client, so it has (read) access to this data.

[accept]: https://man7.org/linux/man-pages/man2/accept.2.html

## Android Platform Details

On Android, in order to pass platform objects, allow for service activation, and
fit better within the idioms of the platform, Monado provides a Binder/AIDL
service instead of a named socket. (The named sockets we typically use are not
permitted by the platform, and "abstract" named sockets are currently available,
but are not idiomatic for the platform and lack other useful capabilities.)
Specifically, we provide a [foreground and started][foreground] (to be able to
display), [bound][bound_service] [service][android_service] with an interface
defined using [AIDL][]. (See also
[this third-party guide about such AIDL services][AidlServices]) This is not
like the system services which provide hardware data or system framework data
from native code. this has a Java (JVM/Dalvik/ART) component provided by code in
an APK, exposed by properties in the package manifest.

[NdkBinder][] is not used because it is mainly suitable for the system type of
binder services. An APK-based service would still require some JVM code to
expose it, and since the AIDL service is used for so little, mixing languages
did not make sense.

The service we expose provides an implementation of our AIDL-described
interface, `org.freedesktop.monado.ipc.IMonado`. This can be modified freely, as
both the client and server are built at the same time and packaged in the same
APK, even though they get loaded in different processes.

[foreground]: https://developer.android.com/guide/components/foreground-services
[bound_service]: https://developer.android.com/guide/components/bound-services
[android_service]: https://developer.android.com/guide/components/services
[aidl]: https://developer.android.com/guide/components/aidl
[AidlServices]: https://devarea.com/android-services-and-aidl/
[NdkBinder]: https://developer.android.com/ndk/reference/group/ndk-binder

The first main purpose of this service is for automatic startup and the
**locating** function: helping establish communication between the client and
the service. The Android framework takes care of launching the service process
when the client requests to start and bind our service by name and package. The
framework also provides us with method calls when we're started/bound. In this
way, the "entry point" of the Monado service on Android is the
`org.freedesktop.monado.ipc.MonadoService` class, which exposes the
implementation of our AIDL interface, `org.freedesktop.monado.ipc.MonadoImpl`.

From there, the native-code mainloop starts when this service is started. By
default, the JVM code will signal the mainloop to shut down a short time after
the last client disconnects, to work best within the platform.

At startup, just as on Linux, the shared memory segment is created. The
[ashmem][] API is used to create/destroy an anonymous **shared memory** segment
on Android, instead of standard POSIX shared memory, but is otherwise treated
and used exactly the same as on standard Linux: file descriptors are duplicated
and passed through IPC calls, etc.

When the client side starts up, it creates an __anonymous socket pair__ to use
for IPC calls (the **RPC** function) later. It then passes one of the two file
descriptors into the AIDL method we defined named "connect". This transports the
FD to the service process, which uses it as the unique communication channel for
that client in its own thread. This replaces the socket pair produced by
connecting/accepting the named socket as used in standard Linux.

[ashmem]: https://developer.android.com/ndk/reference/group/memory

The AIDL interface is also used for transporting some platform objects. At this
time, the only one transported in this way is the [Surface][] injected into the
client activity which is used for displaying rendered output.

[Surface]: https://developer.android.com/reference/android/view/Surface

### Synchronization

Synchronization of new client connections is a special challenge on the Android
platform, since new clients arrive via calls into JVM code while the mainloop is
native code. Unlike Linux, we cannot simply use epoll to check if there are new
connections to our locating socket.

We have the following design goals/constraints:

- All we need to communicate is an integer (file descriptor) within a process.
- Make it fast in the server mainloop in the most common case that there are no
  new clients.
  - This suggests that we should be able to check if there may be a waiting
    client in purely native code, without JNI.
- Make it relatively fast in the server mainloop even when there is a client,
  since it's the compositor thread.
  - This might mean we want to do it all without JNI on the main thread.
- The client should know (and be unblocked) when the server has accepted its
  connection.
  - This suggests that the method called in `MonadoImpl` should block until the
    server consumes/accepts the connection.
  - Not 100% sure this is required, but maybe.
- Resources (file descriptors, etc) should not be leaked.
  - Each should have a well-known owner at each point in time.
- It is OK if only one new client is accepted per mainloop.
  - The mainloop is high rate (compositor rate) and new client connections are
    relatively infrequent.

The IPC service creates a pipe as well as some state variables, two mutexes, and a
condition variable.

When the JVM Service code has a new client, it calls
`ipc_server_mainloop_add_fd()` to pass the FD in. It takes two mutexes, in
order: `ipc_server_mainloop::client_push_mutex` and
`ipc_server_mainloop::accept_mutex`. The purpose of
`ipc_server_mainloop::client_push_mutex` is to allow only one client into the
client-acceptance handshake at a time, so that no acknowledgement of client
accept is lost. Once those two mutexes are locked,
`ipc_server_mainloop_add_fd()` writes the FD number to the pipe. Then, it waits
on the condition variable (releasing `accept_mutex`) to see either that FD
number or the special "shutting down" sentinel value in the `last_accepted_fd`
variable. If it sees the FD number, that indicates that the other side of the
communication (the mainloop) has taken ownership of the FD and will handle
closing it. If it sees the sentinel value, or has an error at some point, it
assumes that ownership is retained and it should close the FD itself.

The other side of the communication works as follows: epoll is used to check if
there is new data waiting on the pipe. If so, the
`ipc_server_mainloop::accept_mutex` lock is taken, and an FD number is read from
the pipe. A client thread is launched for that FD, then the `last_accepted_fd`
variable is updated and the `ipc_server_mainloop::accept_cond` condition
variable signalled.

The initial plan required that the server also wait on
`ipc_server_mainloop::accept_cond` for the `last_accepted_fd` to be reset back
to `0` by the acknowledged client, thus preventing losing acknowledgements.
However, it is undesirable for the clients to be able to block the
compositor/server, so this wait was considered not acceptable. Instead, the
`ipc_server_mainloop::client_push_mutex` is used so that at most one
un-acknowledged client may have written to the pipe at any given time.
