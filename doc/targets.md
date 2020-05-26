# Understanding and Writing Targets

Monado is designed to be a collection of related but independent modules. The
final build product that brings all the desired components together, potentially
with additional code, is called the "target". There are several targets included
in the Monado source tree (in `src/xrt/targets/`) including:

- `cli` - builds `monado-cli` executable
- `openxr` - builds `libopenxr-monado.so` OpenXR runtime shared object
- `gui` - builds `monado-gui` executable
- `service` - builds `monado-service` executable (if `XRT_FEATURE_SERVICE` is
  enabled)

There is also a directory `common` which builds two static libraries. Because
the "target" is responsible for pulling in all the desired drivers, etc. it can
lead to some repetition if multiple targets want the same driver collection. For
this reason, the "all drivers" code shared between many targets is located here,
though you could consider it a part of the individual targets.

## Requirements of a Target

A target must first provide the entry point desired: `int main()` if it's an
executable, or the well-known symbol name if it's a shared library. In some
cases, the entry point might be provided by one of the modules being combined to
form the target. For instance, an OpenXR runtime must expose
`xrNegotiateLoaderRuntimeInterface`: this function is provided by the OpenXR
state tracker `st_oxr`, so the OpenXR runtime target just has to link the state
tracker in and ensure it is present in the final build product.

Then, the target must provide access to the collection of devices desired. The
code is currently (26-May-2020) in the middle of a transition, from a
"prober"-centric interface to an "instance"-centric interface.

- Target device access was historically done by implementing
  `xrt_prober_create`, typically through a call to
  `xrt_prober_create_with_lists`, often passing the target list defined in the
  common `target_lists.c` shared file. This is currently still somewhat-required
  but not as central as before, and will be removed soon.
- Target device access is now provided by implementing the `xrt_instance`
  interface in your target and providing a definition of `xrt_instance_create`
  that instantiates your implementation.

All methods of `xrt_instance` are required, though the `get_prober` method may
output a null pointer if the instance is not using a prober. For more detailed
information on this interface, see the documentation for @ref xrt_instance

## Sample Call Trees

For clarity, call trees are included below for the OpenXR runtime in two general
cases: `XRT_FEATURE_SERVICE` disabled, and `XRT_FEATURE_SERVICE` enabled. Note
that even with `XRT_FEATURE_SERVICE` enabled, the other targets (cli, gui) more
closely resembler the `XRT_FEATURE_SERVICE` disabled diagram: they contain the
device drivers internally rather than contacting the service.

### XRT_FEATURE_SERVICE disabled

This is the simplest architecture. It is also the architecture used by the
various extra targets like `monado-cli` even when building with
`XRT_FEATURE_SERVICE` enabled.

![In-process OpenXR runtime diagram](images/in-process.svg)

### XRT_FEATURE_SERVICE enabled

Note that in this case, there are two processes involved, which have different
`xrt_instance` implementations.

- The runtime has a "stub" or "client proxy" implementation that delegates to
  the service over the IPC.
- The service has a normal or complete instance implementation that actually
  provides access to devices, etc.

![Out-of-process OpenXR runtime diagram](images/out-of-proc.svg)
