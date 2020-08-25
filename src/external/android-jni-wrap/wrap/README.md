# About these JNI Wrappers

<!--
Copyright 2020, Collabora, Ltd.
SPDX-License-Identifier: BSL-1.0
-->

These are fairly simple wrappers around Java classes, using JNI and JNIPP to allow relatively-painless use of Java classes from C++. They are populated as-needed: if you need a method/property that's missing, add it!

## Conventions

All classes derive from ObjectWrapperBase or one of its subclasses. (Yes, you
can directly mirror inheritance in Java with inheritance in these wrappers.)

All should have a public internal struct called `Meta` that derives publicly
from either `MetaBase` or `MetaBaseDroppable` (the more common option, when you
don't often need the class object itself), with a member for each method ID.
Only the `Meta()` constructor should be in the `.cpp` file for a given wrapper:
the rest lives in the header so it may be inlined and thus optimized away.

## Finding signatures

A command like the following can help you get the JNI signatures of methods:

```sh
javap -s -classpath ~/Android/Sdk/platforms/android-26/android.jar android.service.vr.VrListenerService
```

Adjust the location of the SDK and the class under investigation as needed.
