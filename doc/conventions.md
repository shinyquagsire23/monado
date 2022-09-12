# Code Style and Conventions {#conventions}

<!--
Copyright 2021-2022, Collabora, Ltd. and the Monado contributors
SPDX-License-Identifier: BSL-1.0
-->

<!--

NOTE to editors: To avoid stale references, make sure to mention relevant names
using markup like @ref xrt_device so that Doxygen tries to parse and link names.
This will result in Doxygen warnings if we change the name of something
mentioned in these examples.

-->

Here are some general code style guidelines we follow.

Note that we aim to "code with respect", to avoid terminology that may limit our
community or hurt those in it, as well as to conform with emerging industry
standards. Good guidelines to look to include the
[Android Coding with Respect][] policy and the
[Write Inclusive Documentation][] page from the Google developer documentation
style guide. The latter also links to a word list for clear documentation,
which, while not binding on this project, is useful in making sure your code,
comments, and docs are understandable by the worldwide Monado community.

[Android Coding with Respect]: https://source.android.com/setup/contribute/respectful-code
[Write Inclusive Documentation]: https://developers.google.com/style/inclusive-documentation

## APIs

Internal APIs, when it makes sense, should be C APIs. Headers that define
general communication interfaces between modules (not only use of utilities)
belong in the `xrt/include/xrt` directory, and should not depend on any other module outside
that directory. (As a historical note: this directory gets its name from a
compressed version of the phrase "XR RunTime", a generic term for Monado and an
early development codename. Also, it's shorter than `monado_` and so nicer to
use in code.)

What follows are some basic API usage rules. Note that all the module usage
relations must be expressed in the build system, so module usage should form a
directed-acyclic-graph.

- Any module can implement or use APIs declared in `xrt/include/xrt`
- Any module (except the `xrt` interface headers themselves) can (and should!)
  use APIs declared in `xrt/auxiliary/util`.
- Any module except for `auxiliary/util` and the `xrt` interface headers
  themselves can use APIs declared in other `xrt/auxiliary` modules.

## Naming

- C APIs:
  - `lower_snake_case` for types and functions.
  - `UPPER_SNAKE_CASE` for macros. e.g. @ref U_TYPED_CALLOC (which is how all
    allocations in C code should be performed)
  - Prefix names with a "namespace" - the library/module where they reside. e.g.
    @ref u_var_add_root, @ref math_pose_validate
    - Related: only things prefixed by `xrt_` belong in the `xrt/include/xrt`
      directory, and nothing named starting with `xrt_` should be declared
      anywhere else. (Interfaces *declared* in `xrt/include/xrt` are
      *implemented* in other modules.)
  - Generally, we do not declare typedefs for `struct` and `enum` types, but
    instead refer to them in long form, saying `struct` or `enum` then the name.
    The exception to not using typedefs is function pointers used as function
    arguments as these become very hard to both read and type out.
  - If a typedef is needed, it should be named ending with `_t`. Function
    pointer typedefs should end with `_func_t`.
  - Parameters: `lower_snake_case` or acronyms.
    - Output parameters should begin with `out_`.
    - Of special note: Structures/types that represent "objects" often have long
      type names or "conceptual" names. When a pointer to them is passed to a
      function or kept as a local variable, it is typically named by taking the
      first letter of each (typically `_`-delimited) word in the structure type
      name. Sometimes, it is an abbreviated form of that name instead. Relevant
      examples:
      - @ref xrt_comp_native_create_swapchain() is a member function of the
        interface @ref xrt_compositor_native, and takes a pointer to that
        interface named `xcn`. It creates an @ref xrt_swapchain, which it
        populates in the parameter named `out_xscn`: `out_` because it's a
        purely output parameter, `xscn` from @ref xrt_swapchain_native
        specifically the letters `Xrt_SwapChain_Native`. @ref xrt_swapchain and
        related types are a small exception to the rules - there are only 2
        words if you go by the `_` delimiters, but for clarity we treat
        swapchain as if it were two words when abbreviating. A few other places
        in the `xrt` headers use `x` + an abbreviated name form, like `xinst`
        for @ref xrt_instance, `xdev` for @ref xrt_device, `xsysd` sometimes
        used for @ref xrt_system_devices.
  - `create` and `destroy` are used when the functions actually perform
    allocation and return the new object, or deallocation of the passed-in
    object.
  - If some initialization or cleanup is required but the type is not opaque and
    is allocated by the caller, the names to use are `init` and, if needed, one
    of `cleanup`/`fini`/`teardown`. (We are not yet consistent on these names.)
    One common example is when there is some shared code and a structure
    partially implementing an interface: a further-derived object may need to
    call an `init` function on the shared structure, but it was allocated by the
    derived object and held by value.
- C++:
  - Where a C API is exposed, it should follow the C API naming schemes.
  - If only a C++ API is exposed, a fairly conventional C++ naming scheme is used:
    - Namespaces: nested to match directory structure, starting with `xrt::`.
      - There are no C++ interfaces in the `xrt/include/xrt`, by design, so this
        is not ambiguous.
      - Place types that need to be exposed in a header for technical reasons,
        but that are still considered implementation details, within a
        further-nested `detail` namespace, as seen elsewhere in the C++
        ecosystem.
    - Types/classes: `CamelCase`
    - Methods/functions: `lowerCamelCase`
    - Constants/constexpr values: `kCamelCase`
    - If a header is only usable from C++ code, it should be named with the
      extension `.hpp` to signify this.
- Math:
  - For different types of transforms `T` between two entities `A` and `B`, try
    to use variable names like `T_A_B` to express the transform such that `B =
    T_A_B * A`. This is equivalent to "`B` expressed w.r.t. `A`" and "the
    transform that converts a point in `B` coordinates into `A` coordinates".
    `T` can be used for 4x4 isometry matrices, but you can use others like
    `P` for poses, `R` for 3x3 rotations, `Q` for quaternion rotations, `t` for
    translations, etc.

## Patterns and Idioms

This is an incomplete list of conventional idioms used in the Monado codebase.

### C "Inheritance" through first struct member

Despite being in C, the design is fairly object-oriented. Types implement
interfaces and derive from other types typically by placing a field of that
parent type/interface as their first element, conventionally named `base`. This
means that a pointer to the derived type, and a pointer to the base type, have
the same value.

For example, consider @ref client_gl_swapchain

- Its first element is named @ref client_gl_swapchain::base and is of type
  @ref xrt_swapchain_gl - meaning that it implements @ref xrt_swapchain_gl
- @ref xrt_swapchain_gl in turn starts with @ref xrt_swapchain_gl::base which is
  @ref xrt_swapchain - meaning that @ref xrt_swapchain_gl **extends** @ref
  xrt_swapchain. (Both @ref xrt_swapchain_gl and @ref xrt_swapchain are abstract
  interfaces, as indicated by the `xrt_` prefix.)

Structures/types that represent "objects" are often passed as the first
parameter to many functions, which serve as their "member functions". Sometimes,
these types are opaque and not related to other types in the system in a
user-visible way: they should have a `_create` and `_destroy` function. See @ref
time_state, @ref time_state_create, @ref time_state_destroy

In other cases, an interface will have function pointers defined as fields in
the interface structure. (A type implementing these may be opaque, but would
begin with a member of the interface/base type.) These interface function
pointers must still take in a self pointer as their first parameter, because
there is no implied `this` pointer in C. This would result in awkward calls with
repeated, error-prone mentions of the object pointer, such as this example
calling the @ref xrt_device::update_inputs interface:
`xdev->update_inputs(xdev)`. These are typically wrapped by inline free
functions that make the call through the function pointer. Considering again the
@ref xrt_device example, the way you would call @ref xrt_device::update_inputs
is actually @ref xrt_device_update_inputs().

### Destroy takes a pointer to a pointer, nulls it out

Destroy free functions should take a pointer to a pointer, performing null checks
before destruction, and setting null. They always succeed (void return): a
failure when destroying an object has little meaning in most cases. For a
sample, see @ref xrt_images_destroy. It would be used like this:

```c
struct xrt_image_native_allocator *xina = /* created and initialized, or maybe NULL */;

/* ... */

xrt_images_destroy(&xina);

/* here, xina is NULL in all cases, and if it wasn't NULL before, it has been freed. */
```

Note that this pattern is used in most cases but not all in the codebase: we
are gradually migrating those that don't fit this pattern. If you call a
destroy function that does not take a pointer-to-a-pointer, make sure to do
null checks before calling and set it to null after it returns.

Also note: when an interface includes a "destroy" function pointer, it takes the
normal pointer to an object: The free function wrapper is the one that takes a
pointer-to-a-pointer and handles the null checks. See for example @ref
xrt_instance_destroy takes the pointer-to-a-pointer, while the interface method
@ref xrt_instance::destroy takes the single pointer.
